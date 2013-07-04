/*
 * Astra SoftCAM Module
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
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
 */

/*
 * Module Name:
 *      decrypt
 *
 * Module Options:
 *      upstream    - object, stream instance returned by module_instance:stream()
 *      name        - string, channel name
 *      biss        - string, BISS key, 16 chars length. example: biss = "1122330044556600"
 *      cam         - object, cam instance returned by cam_module_instance:cam()
 *      cas_data    - string, additional paramters for CAS
 */

// TODO: stream reload

#include <astra.h>
#include "module_cam.h"
#include "cas/cas_list.h"
#include "FFdecsa/FFdecsa.h"

struct module_data_t
{
    MODULE_LUA_DATA();
    MODULE_STREAM_DATA();
    MODULE_DECRYPT_DATA();

    /* Config */
    const char *name;
    int caid;

    /* Buffer */
    uint8_t *buffer; // r_buffer + s_buffer
    uint8_t *r_buffer;
    uint8_t *s_buffer;
    size_t buffer_skip;

    /* FFdecsa */
    int is_keys;
    void *ffdecsa;
    uint8_t **cluster;
    size_t cluster_size;
    size_t cluster_size_bytes;

    int new_key_id; // 0 - not, 1 - first key, 2 - second key
    uint8_t new_key[16];

    /* Base */
    mpegts_psi_t *pat;
    mpegts_psi_t *cat;
    mpegts_psi_t *pmt;
    mpegts_psi_t *custom_pmt;
    mpegts_psi_t *em;

    mpegts_packet_type_t stream[MAX_PID];
};

#define MSG(_msg) "[decrypt %s] " _msg, mod->name

static module_cas_t * module_decrypt_cas_init(module_data_t *mod)
{
    for(int i = 0; cas_init_list[i]; ++i)
    {
        module_cas_t *cas = cas_init_list[i](&mod->__decrypt);
        if(cas)
            return cas;
    }
    return NULL;
}

static void module_decrypt_cas_destroy(module_data_t *mod)
{
    if(!mod->__decrypt.cas)
        return;
    free(mod->__decrypt.cas->self);
    mod->__decrypt.cas = NULL;
}

static void stream_reload(module_data_t *mod)
{
    if(mod->stream[1] == MPEGTS_PACKET_CAT)
        module_stream_demux_leave_pid(mod, 1);

    for(uint16_t i = 2; i < MAX_PID; ++i)
    {
        if(mod->stream[i] & MPEGTS_PACKET_CA)
            module_stream_demux_leave_pid(mod, i);
    }

    memset(mod->stream, 0, sizeof(mod->stream));
    if(mod->__decrypt.cam->is_ready)
        mod->stream[0] = MPEGTS_PACKET_PAT;

    mod->pat->crc32 = 0;
    mod->cat->crc32 = 0;
    mod->pmt->crc32 = 0;

    module_decrypt_cas_destroy(mod);
}

/*
 * oooooooooo   o   ooooooooooo
 *  888    888 888  88  888  88
 *  888oooo88 8  88     888
 *  888      8oooo88    888
 * o888o   o88o  o888o o888o
 *
 */

static void on_pat(void *arg, mpegts_psi_t *psi)
{
    module_data_t *mod = arg;

    // check changes
    const uint32_t crc32 = PSI_GET_CRC32(psi);
    if(crc32 == psi->crc32)
        return;

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
    {
        asc_log_error(MSG("PAT checksum mismatch"));
        return;
    }

    // reload stream
    if(psi->crc32 != 0)
    {
        asc_log_warning(MSG("PAT changed. Reload stream info"));
        stream_reload(mod);
    }

    psi->crc32 = crc32;

    const uint8_t *pointer = PAT_ITEMS_FIRST(psi);
    while(!PAT_ITEMS_EOL(psi, pointer))
    {
        const uint16_t pnr = PAT_ITEMS_GET_PNR(psi, pointer);
        if(pnr)
        {
            mod->__decrypt.pnr = pnr;
            const uint16_t pmt_pid = PAT_ITEMS_GET_PID(psi, pointer);
            mod->stream[pmt_pid] = MPEGTS_PACKET_PMT;
            mod->pmt->pid = pmt_pid;
            break;
        }
        PAT_ITEMS_NEXT(psi, pointer);
    }

    if(mod->__decrypt.cam)
    {
        mod->__decrypt.cas = module_decrypt_cas_init(mod);
        asc_assert(mod->__decrypt.cas != NULL, "CAS with CAID:0x%04X not found", mod->caid);

        if(!mod->__decrypt.cam->disable_emm)
        {
            mod->stream[1] = MPEGTS_PACKET_CAT;
            module_stream_demux_join_pid(mod, 0);
        }
    }
}

/*
 *   oooooooo8     o   ooooooooooo
 * o888     88    888  88  888  88
 * 888           8  88     888
 * 888o     oo  8oooo88    888
 *  888oooo88 o88o  o888o o888o
 *
 */

static void on_cat(void *arg, mpegts_psi_t *psi)
{
    module_data_t *mod = arg;

    // check changes
    const uint32_t crc32 = PSI_GET_CRC32(psi);
    if(crc32 == psi->crc32)
        return;

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
    {
        asc_log_error(MSG("PMT checksum mismatch"));
        return;
    }

    // reload stream
    if(psi->crc32 != 0)
    {
        asc_log_warning(MSG("CAT changed. Reload stream info"));
        stream_reload(mod);
        return;
    }

    psi->crc32 = crc32;

    const uint8_t *desc_pointer = CAT_DESC_FIRST(psi);
    while(!CAT_DESC_EOL(psi, desc_pointer))
    {
        if(desc_pointer[0] == 0x09
           && DESC_CA_CAID(desc_pointer) == mod->caid
           && module_cas_check_descriptor(mod->__decrypt.cas, desc_pointer))
        {
            const uint16_t pid = DESC_CA_PID(desc_pointer);
            if(mod->stream[pid] == MPEGTS_PACKET_UNKNOWN)
            {
                mod->stream[pid] = MPEGTS_PACKET_EMM;
                module_stream_demux_join_pid(mod, pid);
            }
        }
        CAT_DESC_NEXT(psi, desc_pointer);
    }
}

/*
 * oooooooooo oooo     oooo ooooooooooo
 *  888    888 8888o   888  88  888  88
 *  888oooo88  88 888o8 88      888
 *  888        88  888  88      888
 * o888o      o88o  8  o88o    o888o
 *
 */

static void on_pmt(void *arg, mpegts_psi_t *psi)
{
    module_data_t *mod = arg;

    // check pnr
    const uint16_t pnr = PMT_GET_PNR(psi);
    if(pnr != mod->__decrypt.pnr)
        return;

    // check changes
    const uint32_t crc32 = PSI_GET_CRC32(psi);
    if(crc32 == psi->crc32)
    {
        mpegts_psi_demux(mod->custom_pmt
                         , (void (*)(void *, const uint8_t *))__module_stream_send
                         , &mod->__stream);
        return;
    }

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
    {
        asc_log_error(MSG("PMT checksum mismatch"));
        return;
    }

    // reload stream
    if(psi->crc32 != 0)
    {
        asc_log_warning(MSG("PMT changed. Reload stream info"));
        stream_reload(mod);
        return;
    }

    psi->crc32 = crc32;

    // Make custom PMT and set descriptors for CAS
    mod->custom_pmt->pid = psi->pid;

    uint16_t skip = 12;
    memcpy(mod->custom_pmt->buffer, psi->buffer, 10);

    const uint8_t *desc_pointer = PMT_DESC_FIRST(psi);
    while(!PMT_DESC_EOL(psi, desc_pointer))
    {
        if(desc_pointer[0] == 0x09)
        {
            if(mod->caid != 0x2600 /* Not BISS */
               && DESC_CA_CAID(desc_pointer) == mod->caid
               && module_cas_check_descriptor(mod->__decrypt.cas, desc_pointer))
            {
                const uint16_t pid = DESC_CA_PID(desc_pointer);
                if(mod->stream[pid] == MPEGTS_PACKET_UNKNOWN)
                {
                    mod->stream[pid] = MPEGTS_PACKET_ECM;
                    module_stream_demux_join_pid(mod, pid);
                }
            }
        }
        else
        {
            const uint8_t size = desc_pointer[1] + 2;
            memcpy(&mod->custom_pmt->buffer[skip], desc_pointer, size);
            skip += size;
        }

        PMT_DESC_NEXT(psi, desc_pointer);
    }
    const uint16_t size = skip - 12; // 12 - PMT header
    mod->custom_pmt->buffer[10] = (mod->pmt->buffer[10] & 0xF0) | ((size >> 8) & 0x0F);
    mod->custom_pmt->buffer[11] = size & 0xFF;

    const uint8_t *pointer = PMT_ITEMS_FIRST(psi);
    while(!PMT_ITEMS_EOL(psi, pointer))
    {
        memcpy(&mod->custom_pmt->buffer[skip], pointer, 5);
        skip += 5;

        const uint16_t skip_last = skip;

        desc_pointer = PMT_ITEM_DESC_FIRST(pointer);
        while(!PMT_ITEM_DESC_EOL(pointer, desc_pointer))
        {
            if(desc_pointer[0] == 0x09)
            {
                if(mod->caid != 0x2600 /* Not BISS */
                   && DESC_CA_CAID(desc_pointer) == mod->caid
                   && module_cas_check_descriptor(mod->__decrypt.cas, desc_pointer))
                {
                    const uint16_t pid = DESC_CA_PID(desc_pointer);
                    if(mod->stream[pid] == MPEGTS_PACKET_UNKNOWN)
                    {
                        mod->stream[pid] = MPEGTS_PACKET_ECM;
                        module_stream_demux_join_pid(mod, pid);
                    }
                }
            }
            else
            {
                const uint8_t size = desc_pointer[1] + 2;
                memcpy(&mod->custom_pmt->buffer[skip], desc_pointer, size);
                skip += size;
            }

            PMT_ITEM_DESC_NEXT(pointer, desc_pointer);
        }
        const uint16_t size = skip - skip_last;
        mod->custom_pmt->buffer[skip_last - 2] = (size << 8) & 0x0F;
        mod->custom_pmt->buffer[skip_last - 1] = size & 0xFF;

        PMT_ITEMS_NEXT(psi, pointer);
    }

    mod->custom_pmt->buffer_size = skip + CRC32_SIZE;
    PSI_SET_SIZE(mod->custom_pmt);
    PSI_SET_CRC32(mod->custom_pmt);

    mpegts_psi_demux(mod->custom_pmt
                     , (void (*)(void *, const uint8_t *))__module_stream_send
                     , &mod->__stream);
}

/*
 * ooooooooooo oooo     oooo
 *  888    88   8888o   888
 *  888ooo8     88 888o8 88
 *  888    oo   88  888  88
 * o888ooo8888 o88o  8  o88o
 *
 */

static void on_em(void *arg, mpegts_psi_t *psi)
{
    module_data_t *mod = arg;

    if(psi->buffer_size > EM_MAX_SIZE)
    {
        asc_log_error(MSG("Entitlement message size is greater than %d"), EM_MAX_SIZE);
        return;
    }

    const uint8_t em_type = psi->buffer[0];
    if((em_type & ~0x0F) != 0x80)
    {
        asc_log_error(MSG("wrong packet type 0x%02X"), em_type);
        return;
    }
    else if(em_type >= 0x82)
    { /* EMM */
        if(mod->__decrypt.cam->disable_emm)
            return;
    }
    else
    { /* ECM */
        ;
    }

    if(!module_cas_check_em(mod->__decrypt.cas, psi))
        return;

    mod->__decrypt.cam->send_em(mod->__decrypt.cam->self, &mod->__decrypt
                                , psi->buffer, psi->buffer_size);
}

/*
 * ooooooooooo  oooooooo8
 * 88  888  88 888
 *     888      888oooooo
 *     888             888
 *    o888o    o88oooo888
 *
 */

static void on_ts(module_data_t *mod, const uint8_t *ts)
{
    const uint16_t pid = TS_PID(ts);

    switch(mod->stream[pid])
    {
        case MPEGTS_PACKET_PAT:
            mpegts_psi_mux(mod->pat, ts, on_pat, mod);
            break;
        case MPEGTS_PACKET_CAT:
            mpegts_psi_mux(mod->cat, ts, on_cat, mod);
            return;
        case MPEGTS_PACKET_PMT:
            mpegts_psi_mux(mod->pmt, ts, on_pmt, mod);
            return;
        case MPEGTS_PACKET_ECM:
        case MPEGTS_PACKET_EMM:
            if(!mod->__decrypt.cas)
                return;
            mpegts_psi_mux(mod->em, ts, on_em, mod);
            return;
        default:
            break;
    }

    if(!mod->is_keys)
    {
        module_stream_send(mod, ts);
        return;
    }

    memcpy(&mod->r_buffer[mod->buffer_skip], ts, TS_PACKET_SIZE);
    if(mod->s_buffer)
        module_stream_send(mod, &mod->s_buffer[mod->buffer_skip]);

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
    if(mod->new_key_id)
    {
        if(mod->new_key_id == 1)
            set_even_control_word(mod->ffdecsa, &mod->new_key[0]);
        else if(mod->new_key_id == 2)
            set_odd_control_word(mod->ffdecsa, &mod->new_key[8]);

        mod->new_key_id = 0;
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

/*
 *      o      oooooooooo ooooo
 *     888      888    888 888
 *    8  88     888oooo88  888
 *   8oooo88    888        888
 * o88o  o888o o888o      o888o
 *
 */

static void on_cam_ready(module_data_t *mod)
{
    mod->caid = mod->__decrypt.cam->caid;
    stream_reload(mod);
}

static void on_cam_error(module_data_t *mod)
{
    mod->caid = 0x0000;
    mod->is_keys = false;
}

static void on_response(module_data_t *mod, const uint8_t *data, const char *errmsg)
{
    if((data[0] & ~0x01) != 0x80)
        return; /* Skip EMM */

    bool is_keys_ok = false;
    do
    {
        if(errmsg)
            break;

        if(!module_cas_check_keys(mod->__decrypt.cas, data))
        {
            errmsg = "Wrong ECM format";
            break;
        }

        if(data[2] != 16)
        {
            errmsg = "Wrong ECM length";
            break;
        }

        static const char *errmsg_checksum = "Wrong ECM checksum";
        const uint8_t ck1 = (data[3] + data[4] + data[5]) & 0xFF;
        if(ck1 != data[6])
        {
            errmsg = errmsg_checksum;
            break;
        }

        const uint8_t ck2 = (data[7] + data[8] + data[9]) & 0xFF;
        if(ck2 != data[10])
        {
            errmsg = errmsg_checksum;
            break;
        }

        is_keys_ok = true;
    } while(0);

    if(is_keys_ok)
    {
        // Set keys
        if(mod->new_key[3] == data[6] && mod->new_key[7] == data[10])
        {
            mod->new_key_id = 2;
            memcpy(&mod->new_key[8], &data[11], 8);
        }
        else if(mod->new_key[11] == data[14] && mod->new_key[15] == data[18])
        {
            mod->new_key_id = 1;
            memcpy(mod->new_key, &data[3], 8);
        }
        else
        {
            mod->new_key_id = 0;
            set_control_words(mod->ffdecsa, &data[3], &data[11]);
            memcpy(mod->new_key, &data[3], 16);
            if(mod->is_keys)
                asc_log_warning(MSG("Both keys changed"));
            else
                mod->is_keys = 1;
        }

#if CAS_ECM_DUMP
        char key_1[17], key_2[17];
        hex_to_str(key_1, &data[3], 8);
        hex_to_str(key_2, &data[11], 8);
        asc_log_debug(MSG("ECM Found [%02X:%s:%s]") , data[0], key_1, key_2);
#endif
    }
    else
    {
        if(!errmsg)
            errmsg = "Unknown";
        asc_log_error(MSG("ECM 0x%02X Not Found. %s"), data[0], errmsg);
    }
}

/*
 * oooo     oooo  ooooooo  ooooooooo  ooooo  oooo ooooo       ooooooooooo
 *  8888o   888 o888   888o 888    88o 888    88   888         888    88
 *  88 888o8 88 888     888 888    888 888    88   888         888ooo8
 *  88  888  88 888o   o888 888    888 888    88   888      o  888    oo
 * o88o  8  o88o  88ooo88  o888ooo88    888oo88   o888ooooo88 o888ooo8888
 *
 */

static void module_init(module_data_t *mod)
{
    module_stream_init(mod, on_ts);

    module_option_string("name", &mod->name);
    asc_assert(mod->name != NULL, "[decrypt] option 'name' is required");

    mod->ffdecsa = get_key_struct();
    mod->cluster_size = get_suggested_cluster_size();
    mod->cluster_size_bytes = mod->cluster_size * TS_PACKET_SIZE;
    mod->cluster = malloc(sizeof(void *) * (mod->cluster_size * 2 + 2));

    uint8_t first_key[8] = { 0 };
    const char *string_value = NULL;
    const int biss_length = module_option_string("biss", &string_value);
    if(string_value)
    {
        if(biss_length != 16)
        {
            asc_log_error(MSG("biss key must be 16 chars length"));
            astra_abort();
        }
        str_to_hex(string_value, first_key, sizeof(first_key));
        first_key[3] = (first_key[0] + first_key[1] + first_key[2]) & 0xFF;
        first_key[7] = (first_key[4] + first_key[5] + first_key[6]) & 0xFF;
        mod->is_keys = 1;
        mod->caid = 0x2600;
    }
    set_control_words(mod->ffdecsa, first_key, first_key);

    // module_decrypt_init(mod, on_cam_ready, on_cam_error);
    mod->__decrypt.self = mod;
    mod->__decrypt.on_cam_ready = on_cam_ready;
    mod->__decrypt.on_cam_error = on_cam_error;
    mod->__decrypt.on_response = on_response;

    if(!mod->is_keys)
    {
        lua_getfield(lua, 2, "cam");
        if(!lua_isnil(lua, -1))
        {
            asc_assert(lua_type(lua, -1) == LUA_TLIGHTUSERDATA
                       , "option 'cam' required cam-module instance");
            mod->__decrypt.cam = lua_touserdata(lua, -1);
        }
        lua_pop(lua, 1);
    }

    if(mod->__decrypt.cam)
    {
        const char *value = NULL;
        module_option_string("cas_data", &value);
        if(value)
            str_to_hex(value, mod->__decrypt.cas_data, sizeof(mod->__decrypt.cas_data));

        module_cam_attach_decrypt(mod->__decrypt.cam, &mod->__decrypt);
    }
    // ---

    mod->buffer = malloc(mod->cluster_size_bytes * 2);
    mod->r_buffer = mod->buffer; // s_buffer = NULL

    mod->pat = mpegts_psi_init(MPEGTS_PACKET_PAT, 0);
    mod->cat = mpegts_psi_init(MPEGTS_PACKET_CAT, 1);
    mod->pmt = mpegts_psi_init(MPEGTS_PACKET_PMT, MAX_PID);
    mod->em = mpegts_psi_init(MPEGTS_PACKET_CA, MAX_PID);
    mod->custom_pmt = mpegts_psi_init(MPEGTS_PACKET_PMT, MAX_PID);
}

static void module_destroy(module_data_t *mod)
{
    module_stream_destroy(mod);

    if(mod->__decrypt.cam)
    {
        module_cam_detach_decrypt(mod->__decrypt.cam, &mod->__decrypt);
        module_decrypt_cas_destroy(mod);
    }

    free_key_struct(mod->ffdecsa);
    free(mod->cluster);
    free(mod->buffer);

    mpegts_psi_destroy(mod->pat);
    mpegts_psi_destroy(mod->cat);
    mpegts_psi_destroy(mod->pmt);
    mpegts_psi_destroy(mod->em);
    mpegts_psi_destroy(mod->custom_pmt);
}

MODULE_STREAM_METHODS()
MODULE_LUA_METHODS()
{
    MODULE_STREAM_METHODS_REF()
};
MODULE_LUA_REGISTER(decrypt)
