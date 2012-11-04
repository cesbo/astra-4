/*
 * Astra SoftCAM module
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 *
 * This module is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This module is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this module.  If not, see <http://www.gnu.org/licenses/>.
 *
 * For more information, visit http://cesbo.com
 */

#include "softcam.h"
#include <modules/utils/utils.h>
#include "FFdecsa/FFdecsa.h"

// TODO: threading

#define LOG_MSG(_msg) "[decrypt %s] " _msg, mod->config.name

struct module_data_s
{
    MODULE_BASE();
    MODULE_EVENT_BASE();

    struct
    {
        const char *name;
        int real_pnr;
        int ecm_pid;
        int fake;
    } config;

    int stream_reload;
    mpegts_stream_t stream;
    mpegts_psi_t *custom_pmt;

    cas_data_t *cas;
    module_data_t *cam_mod;

    int is_keys;
    int is_cam_ready;

    uint8_t *buffer; // r_buffer + s_buffer
    uint8_t *r_buffer;
    uint8_t *s_buffer;
    size_t buffer_skip;

    void *ffdecsa;
    uint8_t **cluster;
    size_t cluster_size;
    size_t cluster_size_bytes;

    int is_new_key; // 0 - not, 1 - first key, 2 - second key
    uint8_t new_key[16];
};

/* module code */

static void scan_pat(module_data_t *mod, mpegts_psi_t *psi)
{
    if(mod->stream_reload)
        return;
    mpegts_pat_parse(psi);
    switch(psi->status)
    {
        case MPEGTS_UNCHANGED:
            return;
        case MPEGTS_ERROR_NONE:
            break;
        case MPEGTS_CRC32_CHANGED:
            log_info(LOG_MSG("PAT changed, reload stream info"));
            mod->stream_reload = 1;
        default:
            return;
    }

    mpegts_pat_t *pat = psi->data;
    list_t *i = list_get_first(pat->items);
    while(i)
    {
        mpegts_pat_item_t *item = list_get_data(i);
        if(item->pnr > 0)
        {
            const uint16_t pmt_pid = item->pid;
            mod->stream[pmt_pid] = mpegts_pmt_init(pmt_pid);
        }
        i = list_get_next(i);
    }
} /* scan_pat */

static void scan_cat(module_data_t *mod, mpegts_psi_t *psi)
{
    if(mod->stream_reload)
        return;
    mpegts_cat_parse(psi);
    switch(psi->status)
    {
        case MPEGTS_UNCHANGED:
            return;
        case MPEGTS_ERROR_NONE:
            break;
        case MPEGTS_CRC32_CHANGED:
            log_info(LOG_MSG("CAT changed, reload stream info"));
            mod->stream_reload = 1;
        default:
            return;
    }

    mpegts_cat_t *cat = psi->data;
    cas_data_t *cas = mod->cas;
    list_t *i = list_get_first(cat->desc->items);
    while(i)
    {
        const int emm_pid = cas_check_descriptor(cas, list_get_data(i));
        if(emm_pid > 0 && emm_pid < 0x2000 && !mod->stream[emm_pid])
        {
            log_info(LOG_MSG("select EMM pid:%d"), emm_pid);
            mod->stream[emm_pid] = mpegts_psi_init(MPEGTS_PACKET_EMM, emm_pid);
            stream_ts_join_pid(mod, emm_pid);
        }
        i = list_get_next(i);
    }
} /* scan_cat */

static int _pmt_check_desc(module_data_t *mod, uint8_t *desc)
{
    if(mod->config.ecm_pid > 0)
    {
        const uint16_t ecm_pid = CA_DESC_PID(desc);
        return (mod->config.ecm_pid == ecm_pid) ? 1 : 0;
    }

    const uint16_t ecm_pid = cas_check_descriptor(mod->cas, desc);
    if(ecm_pid > 0 && ecm_pid < MAX_PID && !mod->stream[ecm_pid])
    {
        log_info(LOG_MSG("select ECM pid:%d"), ecm_pid);
        mod->stream[ecm_pid] = mpegts_psi_init(MPEGTS_PACKET_ECM, ecm_pid);
        stream_ts_join_pid(mod, ecm_pid);
        return 1;
    }
    return 0;
}

static void _pmt_desc_clean(mpegts_desc_t *desc)
{
    int c = 0;
    list_t *i = list_get_first(desc->items);
    while(i)
    {
        uint8_t *b = list_get_data(i);
        if(b[0] == 0x09)
        {
            ++c;
            if(i == desc->items)
            {
                i = list_delete(i, NULL);
                desc->items = i;
            }
            else
                i = list_delete(i, NULL);
        }
        else
            i = list_get_next(i);
    }
    if(c > 0)
        mpegts_desc_assemble(desc);
}

static void scan_pmt(module_data_t *mod, mpegts_psi_t *psi)
{
    if(mod->stream_reload)
        return;
    mpegts_pmt_parse(psi); // TODO: move to mpegts_psi_mux
    switch(psi->status)
    {
        case MPEGTS_UNCHANGED:
            mpegts_psi_demux(mod->custom_pmt, stream_ts_send, mod);
            return;
        case MPEGTS_ERROR_NONE:
            break;
        case MPEGTS_CRC32_CHANGED:
            log_info(LOG_MSG("PMT changed, reload stream info"));
            mod->stream_reload = 1;
        default:
            return;
    }

    mpegts_pmt_t *pmt = psi->data;

    // init CAS
    const uint16_t cas_pnr = (mod->config.real_pnr > 0)
                           ? mod->config.real_pnr
                           : pmt->pnr;
    mod->cas = cas_init(mod, mod->cam_mod, cas_pnr);
    if(!mod->cas)
    {
        log_error(LOG_MSG("cas with caid:0x%04X is not found")
                  , cam_caid(mod->cam_mod));
        mpegts_stream_destroy(mod->stream);
        return;
    }
    else
    {
        log_info(LOG_MSG("%s selected. caid:0x%04X")
                 , cas_name(mod->cas), cam_caid(mod->cam_mod));
    }

    int ecm_count = 0;
    list_t *i = NULL;

    // try to get ECM from PMT descriptors
    if(pmt->desc)
    {
        i = list_get_first(pmt->desc->items);
        while(i)
        {
            ecm_count += _pmt_check_desc(mod, list_get_data(i));
            i = list_get_next(i);
        }
    }

    // if ECM isn't found in PMT descriptors, try to get ECM from ES descriptors
    if(!ecm_count)
    {
        i = list_get_first(pmt->items);
        while(i)
        {
            mpegts_pmt_item_t *item = list_get_data(i);
            i = list_get_next(i);
            if(!item->desc)
                continue;
            list_t *ii = list_get_first(item->desc->items);
            while(ii)
            {
                ecm_count += _pmt_check_desc(mod, list_get_data(ii));
                ii = list_get_next(ii);
            }
        }
    }

    // join CAT if ECM found
    if(ecm_count > 0 && !cam_disable_emm(mod->cam_mod))
    {
        mod->stream[1] = mpegts_cat_init();
        stream_ts_join_pid(mod, 1);
    }

    // make custom PMT and clean CA descriptors
    mod->custom_pmt = mpegts_pmt_duplicate(psi);
    mpegts_pmt_t *custom_pmt = mod->custom_pmt->data;
    if(custom_pmt->desc)
        _pmt_desc_clean(custom_pmt->desc);
    i = list_get_first(custom_pmt->items);
    while(i)
    {
        mpegts_pmt_item_t *item = list_get_data(i);
        if(item->desc)
            _pmt_desc_clean(item->desc);
        i = list_get_next(i);
    }
    mpegts_pmt_assemble(mod->custom_pmt);
    mpegts_psi_demux(mod->custom_pmt, stream_ts_send, mod);
} /* scan_pmt */

static void scan_ecm_emm(module_data_t *mod, mpegts_psi_t *psi)
{
    if(mod->stream_reload)
        return;
    cam_send(mod->cam_mod, mod->cas, psi->buffer);
} /* scan_ecm_emm */

/* stream_ts callbacks */

static void callback_send_ts(module_data_t *mod, uint8_t *ts)
{
    if(mod->stream_reload)
    {
        mpegts_stream_destroy(mod->stream);
        stream_ts_leave_all(mod);

        mod->is_keys = 0;
        cas_destroy(mod->cas);
        mod->cas = NULL;

        mod->stream[0] = mpegts_pat_init();
        mod->stream_reload = 0;
    }

    if(!mod->cam_mod)
    {
        stream_ts_send(mod, ts);
        return;
    }

    const uint16_t pid = TS_PID(ts);

    mpegts_psi_t *psi = mod->stream[pid];
    if(psi)
    {
        switch(psi->type)
        {
            case MPEGTS_PACKET_PAT:
                mpegts_psi_mux(psi, ts, scan_pat, mod);
                break;
            case MPEGTS_PACKET_CAT:
                mpegts_psi_mux(psi, ts, scan_cat, mod);
                return;
            case MPEGTS_PACKET_PMT:
                mpegts_psi_mux(psi, ts, scan_pmt, mod);
                return;
            case MPEGTS_PACKET_EMM:
            case MPEGTS_PACKET_ECM:
                if(mod->cas)
                    mpegts_psi_mux(psi, ts, scan_ecm_emm, mod);
                return;
            default:
                return;
        }
    }

    if(mod->config.fake)
        return;

    if(!mod->is_keys)
    {
        stream_ts_send(mod, ts);
        return;
    }

    memcpy(&mod->r_buffer[mod->buffer_skip], ts, TS_PACKET_SIZE);

    if(mod->s_buffer)
        stream_ts_send(mod, &mod->s_buffer[mod->buffer_skip]);

    mod->buffer_skip += TS_PACKET_SIZE;
    if(mod->buffer_skip < mod->cluster_size_bytes)
        return;

    // fill cluster
    size_t i = 0, p = 0;
    mod->cluster[p] = 0;
    for(; i < mod->cluster_size_bytes; i += TS_PACKET_SIZE, p += 2)
    {
        mod->cluster[p  ] = &mod->r_buffer[i];
        mod->cluster[p+1] = &mod->r_buffer[i+TS_PACKET_SIZE];
    }
    mod->cluster[p] = 0;

    // decrypt
    i = 0;
    while(i < mod->cluster_size)
        i += decrypt_packets(mod->ffdecsa, mod->cluster);

    // check new key
    if(mod->is_new_key)
    {
        if(mod->is_new_key == 1)
            set_even_control_word(mod->ffdecsa, &mod->new_key[0]);
        else if(mod->is_new_key == 2)
            set_odd_control_word(mod->ffdecsa, &mod->new_key[8]);

        mod->is_new_key = 0;
    }

    // swap buffers
    uint8_t *tmp = mod->r_buffer;
    if(mod->s_buffer)
        mod->r_buffer = mod->s_buffer;
    else
        mod->r_buffer = &mod->buffer[mod->cluster_size_bytes];
    mod->s_buffer = tmp;

    mod->buffer_skip = 0;
}

static void callback_on_attach(module_data_t *mod, module_data_t *parent)
{
}

static void callback_on_detach(module_data_t *mod, module_data_t *parent)
{
}

static void callback_join_pid(module_data_t *mod
                              , module_data_t *child
                              , uint16_t pid)
{
    if(pid == 1)
        return;
    stream_ts_join_pid(mod, pid);
}

static void callback_leave_pid(module_data_t *mod
                               , module_data_t *child
                               , uint16_t pid)
{
    if(pid == 1)
        return;
    stream_ts_leave_pid(mod, pid);
}

/* softcam callbacks */

static void interface_set_keys(module_data_t *mod, uint8_t *data, int is_ok)
{
    if(is_ok)
    {
#ifdef CAS_ECM_DUMP
        char ecm_dump[34];
        log_info(LOG_MSG("CW: 0x%02X:%s:%s"), data[0]
                 , hex_to_str(&ecm_dump[0 ], &data[3 ], 8)
                 , hex_to_str(&ecm_dump[17], &data[11], 8));
#endif
        uint8_t *key = &data[3];
        if(!mod->is_keys)
        {
            mod->is_keys = 1;
            mod->is_new_key = 0;
            set_control_words(mod->ffdecsa, &key[0], &key[8]);
            memcpy(mod->new_key, key, 16);
            module_event_call(mod);
        }
        else
        {
            if(mod->new_key[3] == key[3] && mod->new_key[7] == key[7])
            {
                mod->is_new_key = 2;
                memcpy(&mod->new_key[8], &key[8], 8);
            }
            else if(mod->new_key[11] == key[11] && mod->new_key[15] == key[15])
            {
                mod->is_new_key = 1;
                memcpy(mod->new_key, key, 8);
            }
            else
            {
                mod->is_new_key = 0;
                set_control_words(mod->ffdecsa, &key[0], &key[8]);
                memcpy(mod->new_key, key, 16);
                log_warning(LOG_MSG("both keys changed"));
            }
        }
    }
    else
    {
        if(data[2] == 16)
        {
            char ecm_dump[34];
            log_error(LOG_MSG("CW: wrong key 0x%02X:%s:%s"), data[0]
                      , hex_to_str(&ecm_dump[0 ], &data[3 ], 8)
                      , hex_to_str(&ecm_dump[17], &data[11], 8));
        }
        else
        {
            log_error(LOG_MSG("CW: wrong key 0x%02X length:%02X")
                      , data[0], data[1], data[2]);
        }

        if(mod->is_keys)
        {
            mod->is_keys = 0;
            module_event_call(mod);
        }
    }
}

static void interface_cam_status(module_data_t *mod, int is_ready)
{
    mod->is_cam_ready = is_ready;

    if(is_ready <= 0)
    {
        if(mod->cas)
        {
            mod->is_keys = 0;
            cas_destroy(mod->cas);
            mod->cas = NULL;
        }

        if(mod->stream[0])
        {
            if(mod->stream[1])
                stream_ts_leave_pid(mod, 1);

            // PID >= 0x10 May be assigned as NIP, PMT, ES, or for others
            for(int i = 0x10; i < NULL_TS_PID; ++i)
            {
                mpegts_psi_t *p = mod->stream[i];
                if(p && (p->type & MPEGTS_PACKET_CA))
                    stream_ts_leave_pid(mod, i);
            }

            mpegts_stream_destroy(mod->stream);
        }

        if(is_ready < 0)
        {
            mod->cam_mod = NULL;
            mod->is_cam_ready = 0;
        }
        else
            module_event_call(mod);
    }
    else if(!mod->stream[0])
        mod->stream[0] = mpegts_pat_init();
}

/* methods */

static int method_attach(module_data_t *mod)
{
    stream_ts_attach(mod);
    return 0;
}

static int method_detach(module_data_t *mod)
{
    stream_ts_detach(mod);
    return 0;
}

static int method_status(module_data_t *mod)
{
    lua_State *L = LUA_STATE(mod);

    lua_newtable(L);
    lua_pushboolean(L, mod->is_keys);
    lua_setfield(L, -2, "keys");
    lua_pushboolean(L, mod->is_cam_ready);
    lua_setfield(L, -2, "cam");

    return 1;
}

static int method_event(module_data_t *mod)
{
    module_event_set(mod);
    return 0;
}

static int method_cam(module_data_t *mod)
{
    mod->stream_reload = 1;
    lua_State *L = LUA_STATE(mod);

    if(lua_isnil(L, 2))
    {
        if(mod->cam_mod)
        {
            cam_detach_decrypt(mod->cam_mod, mod);
            mod->cam_mod = NULL;
        }
    }
    else
    {
        if(mod->cam_mod)
            cam_detach_decrypt(mod->cam_mod, mod);
        mod->cam_mod = lua_touserdata(L, 2);
        cam_attach_decrypt(mod->cam_mod, mod);
        if(cam_is_ready(mod->cam_mod))
            interface_cam_status(mod, 1);
    }

    return 0;
}

/* required */

static void module_configure(module_data_t *mod)
{
    /*
     * OPTIONS:
     *   name, cam,
     *   real_pnr, ecm_pid, fake
     */

    module_set_string(mod, "name", 1, NULL, &mod->config.name);

    lua_State *L = LUA_STATE(mod);
    lua_getfield(L, 2, "cam");
    if(lua_type(L, -1) != LUA_TNIL)
    {
        if(lua_type(L, -1) != LUA_TUSERDATA)
        {
            log_error(LOG_MSG("option \"cam\" required cam module instance"));
            abort();
        }
        mod->cam_mod = lua_touserdata(L, -1);
    }
    lua_pop(L, 1);

    module_set_number(mod, "real_pnr", 0, 0, &mod->config.real_pnr);
    module_set_number(mod, "ecm_pid", 0, 0, &mod->config.ecm_pid);
    module_set_number(mod, "fake", 0, 0, &mod->config.fake);
}
static void module_initialize(module_data_t *mod)
{
    module_configure(mod);

    stream_ts_init(mod, callback_send_ts
                   , callback_on_attach, callback_on_detach
                   , callback_join_pid, callback_leave_pid);

    DECRYPT_INTERFACE();

    mod->ffdecsa = get_key_struct();
    mod->cluster_size = get_suggested_cluster_size();
    mod->cluster_size_bytes = mod->cluster_size * TS_PACKET_SIZE;
    mod->cluster = malloc(sizeof(void *) * (mod->cluster_size * 2 + 2));
    uint8_t empty_key[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    set_control_words(mod->ffdecsa, empty_key, empty_key);

    if(!mod->config.fake)
    {
        mod->buffer = malloc(mod->cluster_size_bytes * 2);
        mod->r_buffer = mod->buffer; // s_buffer = NULL
    }

    if(mod->cam_mod)
    {
        cam_attach_decrypt(mod->cam_mod, mod);
        if(cam_is_ready(mod->cam_mod))
            interface_cam_status(mod, 1);
    }
}

static void module_destroy(module_data_t *mod)
{
    stream_ts_destroy(mod);
    module_event_destroy(mod);

    if(mod->cas)
        cas_destroy(mod->cas);
    if(mod->cam_mod)
        cam_detach_decrypt(mod->cam_mod, mod);

    if(mod->ffdecsa)
    {
        free_key_struct(mod->ffdecsa);
        free(mod->cluster);
        free(mod->buffer);
    }

    mpegts_stream_destroy(mod->stream);
}

/* config_check */

MODULE_METHODS()
{
    METHOD(attach)
    METHOD(detach)
    METHOD(status)
    METHOD(event)
    METHOD(cam)
};

MODULE(decrypt)
