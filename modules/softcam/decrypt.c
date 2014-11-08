/*
 * Astra Module: SoftCAM. Decrypt Module
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2014, Andrey Dyldin <and@cesbo.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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
 *      cas_pnr     - number, original PNR
 */

#include <astra.h>
#include "module_cam.h"
#include "cas/cas_list.h"

#ifndef FFDECSA
#   define FFDECSA 1
#endif

#ifndef LIBDVBCSA
#   define LIBDVBCSA 0
#endif

#if FFDECSA == 1
#   include "FFdecsa/FFdecsa.h"
#elif LIBDVBCSA == 1
#   include <dvbcsa/dvbcsa.h>
#else
#   error "DVB-CSA is not defined"
#endif

typedef struct
{
    uint8_t ecm_type;
    uint16_t ecm_pid;

    bool is_keys;
    uint8_t parity;

#if FFDECSA == 1

    void *keys;
    uint8_t **batch;

#elif LIBDVBCSA == 1

    struct dvbcsa_bs_key_s *even_key;
    struct dvbcsa_bs_key_s *odd_key;
    struct dvbcsa_bs_batch_s *batch;

#endif

    size_t batch_skip;

    int new_key_id;  // 0 - not, 1 - first key, 2 - second key, 3 - both keys
    uint8_t new_key[16];

    uint64_t sendtime;
} ca_stream_t;

typedef struct
{
    uint16_t es_pid;

    ca_stream_t *ca_stream;
} el_stream_t;

struct module_data_t
{
    MODULE_STREAM_DATA();
    MODULE_DECRYPT_DATA();

    /* Config */
    const char *name;
    int caid;
    bool disable_emm;
    int ecm_pid;

    /* dvbcsa */
    asc_list_t *el_list;
    asc_list_t *ca_list;

    size_t batch_size;

    struct
    {
        uint8_t *buffer;
        size_t size;
        size_t count;
        size_t dsc_count;
        size_t read;
        size_t write;
    } storage;

    struct
    {
        uint8_t *buffer;
        size_t size;
        size_t count;
        size_t read;
        size_t write;
    } shift;

    /* Base */
    mpegts_psi_t *stream[MAX_PID];
    mpegts_psi_t *pmt;
};

#define BISS_CAID 0x2600
#define MSG(_msg) "[decrypt %s] " _msg, mod->name

void ca_stream_set_keys(ca_stream_t *ca_stream, const uint8_t *even, const uint8_t *odd);

ca_stream_t * ca_stream_init(module_data_t *mod, uint16_t ecm_pid)
{
    ca_stream_t *ca_stream;
    asc_list_for(mod->ca_list)
    {
        ca_stream = asc_list_data(mod->ca_list);
#if FFDECSA == 1
        return ca_stream;
#else
        if(ca_stream->ecm_pid == ecm_pid)
            return ca_stream;
#endif
    }

    ca_stream = malloc(sizeof(ca_stream_t));
    memset(ca_stream, 0, sizeof(ca_stream_t));

    ca_stream->ecm_pid = ecm_pid;

#if FFDECSA == 1

    ca_stream->keys = get_key_struct();
    ca_stream->batch = calloc(mod->batch_size * 2 + 2, sizeof(uint8_t *));

#elif LIBDVBCSA == 1

    ca_stream->even_key = dvbcsa_bs_key_alloc();
    ca_stream->odd_key = dvbcsa_bs_key_alloc();
    ca_stream->batch = calloc(mod->batch_size + 1, sizeof(struct dvbcsa_bs_batch_s));

#endif

    asc_list_insert_tail(mod->ca_list, ca_stream);

    return ca_stream;
}

void ca_stream_destroy(ca_stream_t *ca_stream)
{
#if FFDECSA == 1

    free_key_struct(ca_stream->keys);
    free(ca_stream->batch);

#elif LIBDVBCSA == 1

    dvbcsa_bs_key_free(ca_stream->even_key);
    dvbcsa_bs_key_free(ca_stream->odd_key);
    free(ca_stream->batch);

#endif

    free(ca_stream);
}

void ca_stream_set_keys(ca_stream_t *ca_stream, const uint8_t *even, const uint8_t *odd)
{
#if FFDECSA == 1

    if(even)
        set_even_control_word(ca_stream->keys, even);
    if(odd)
        set_odd_control_word(ca_stream->keys, odd);

#elif LIBDVBCSA == 1

    if(even)
        dvbcsa_bs_key_set(even, ca_stream->even_key);
    if(odd)
        dvbcsa_bs_key_set(odd, ca_stream->odd_key);

#endif
}

static void module_decrypt_cas_init(module_data_t *mod)
{
    for(int i = 0; cas_init_list[i]; ++i)
    {
        mod->__decrypt.cas = cas_init_list[i](&mod->__decrypt);
        if(mod->__decrypt.cas)
            return;
    }
    asc_assert(mod->__decrypt.cas != NULL, MSG("CAS with CAID:0x%04X not found"), mod->caid);
}

static void module_decrypt_cas_destroy(module_data_t *mod)
{
    if(mod->__decrypt.cas)
    {
        free(mod->__decrypt.cas->self);
        mod->__decrypt.cas = NULL;
    }

    for(  asc_list_first(mod->el_list)
        ; !asc_list_eol(mod->el_list)
        ; asc_list_remove_current(mod->el_list))
    {
        el_stream_t *el_stream = asc_list_data(mod->el_list);
        free(el_stream);
    }

    if(mod->caid == BISS_CAID)
    {
        asc_list_first(mod->ca_list);
        ca_stream_t *ca_stream = asc_list_data(mod->ca_list);
        ca_stream->batch_skip = 0;
        return;
    }

    for(  asc_list_first(mod->ca_list)
        ; !asc_list_eol(mod->ca_list)
        ; asc_list_remove_current(mod->ca_list))
    {
        ca_stream_t *ca_stream = asc_list_data(mod->ca_list);
        ca_stream_destroy(ca_stream);
    }
}

static void stream_reload(module_data_t *mod)
{
    mod->stream[0]->crc32 = 0;

    for(int i = 1; i < MAX_PID; ++i)
    {
        if(mod->stream[i])
        {
            mpegts_psi_destroy(mod->stream[i]);
            mod->stream[i] = NULL;
        }
    }

    module_decrypt_cas_destroy(mod);

    mod->storage.count = 0;
    mod->storage.dsc_count = 0;
    mod->storage.read = 0;
    mod->storage.write = 0;

    mod->shift.count = 0;
    mod->shift.read = 0;
    mod->shift.write = 0;
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

    const uint8_t *pointer;
    PAT_ITEMS_FOREACH(psi, pointer)
    {
        const uint16_t pnr = PAT_ITEM_GET_PNR(psi, pointer);
        if(pnr == 0)
            continue; // skip NIT

        const uint16_t pid = PAT_ITEM_GET_PID(psi, pointer);
        if(mod->stream[pid])
            asc_log_error(MSG("Skip PMT pid:%d"), pid);
        else
        {
            mod->__decrypt.pnr = pnr;
            if(mod->__decrypt.cas_pnr == 0)
                mod->__decrypt.cas_pnr = pnr;

            mod->stream[pid] = mpegts_psi_init(MPEGTS_PACKET_PMT, pid);
        }

        break;
    }

    if(mod->__decrypt.cam && mod->__decrypt.cam->is_ready)
    {
        module_decrypt_cas_init(mod);
        mod->stream[1] = mpegts_psi_init(MPEGTS_PACKET_CAT, 1);
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

static bool __cat_check_desc(module_data_t *mod, const uint8_t *desc)
{
    const uint16_t pid = DESC_CA_PID(desc);

    /* Skip BISS */
    if(pid == NULL_TS_PID)
        return false;

    if(mod->stream[pid])
    {
        if(!(mod->stream[pid]->type & MPEGTS_PACKET_CA))
        {
            asc_log_warning(MSG("Skip EMM pid:%d"), pid);
            return false;
        }
    }
    else
        mod->stream[pid] = mpegts_psi_init(MPEGTS_PACKET_CA, pid);

    if(mod->disable_emm || mod->__decrypt.cam->disable_emm)
        return false;

    if(   mod->__decrypt.cas
       && DESC_CA_CAID(desc) == mod->caid
       && module_cas_check_descriptor(mod->__decrypt.cas, desc))
    {
        mod->stream[pid]->type = MPEGTS_PACKET_EMM;
        asc_log_info(MSG("Select EMM pid:%d"), pid);
        return true;
    }

    return false;
}

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
        asc_log_error(MSG("CAT checksum mismatch"));
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

    bool is_emm_selected = (mod->disable_emm || mod->__decrypt.cam->disable_emm);

    const uint8_t *desc_pointer;
    CAT_DESC_FOREACH(psi, desc_pointer)
    {
        if(desc_pointer[0] == 0x09)
        {
            if(__cat_check_desc(mod, desc_pointer))
                is_emm_selected = true;
        }
    }

    if(!is_emm_selected)
        asc_log_warning(MSG("EMM is not found"));
}

/*
 * oooooooooo oooo     oooo ooooooooooo
 *  888    888 8888o   888  88  888  88
 *  888oooo88  88 888o8 88      888
 *  888        88  888  88      888
 * o888o      o88o  8  o88o    o888o
 *
 */

static ca_stream_t * __pmt_check_desc(  module_data_t *mod
                                      , const uint8_t *desc
                                      , bool is_ecm_selected)
{
    const uint16_t pid = DESC_CA_PID(desc);

    /* Skip BISS */
    if(pid == NULL_TS_PID)
        return NULL;

    if(mod->stream[pid] == NULL)
        mod->stream[pid] = mpegts_psi_init(MPEGTS_PACKET_CA, pid);

    do
    {
        if(!mod->__decrypt.cas)
            break;
        if(is_ecm_selected)
            break;
        if(!(mod->stream[pid]->type & MPEGTS_PACKET_CA))
            break;

        if(mod->ecm_pid == 0)
        {
            if(DESC_CA_CAID(desc) != mod->caid)
                break;
            if(!module_cas_check_descriptor(mod->__decrypt.cas, desc))
                break;
        }
        else
        {
            if(mod->ecm_pid != pid)
                break;
        }

        asc_list_for(mod->ca_list)
        {
            ca_stream_t *ca_stream = asc_list_data(mod->ca_list);
            if(ca_stream->ecm_pid == pid)
                return ca_stream;
        }

        mod->stream[pid]->type = MPEGTS_PACKET_ECM;
        asc_log_info(MSG("Select ECM pid:%d"), pid);
        return ca_stream_init(mod, pid);
    } while(0);

    asc_log_warning(MSG("Skip ECM pid:%d"), pid);
    return NULL;
}

static void on_pmt(void *arg, mpegts_psi_t *psi)
{
    module_data_t *mod = arg;

    if(psi->buffer[0] != 0x02)
        return;

    // check pnr
    const uint16_t pnr = PMT_GET_PNR(psi);
    if(pnr != mod->__decrypt.pnr)
        return;

    // check changes
    const uint32_t crc32 = PSI_GET_CRC32(psi);
    if(crc32 == psi->crc32)
    {
        mpegts_psi_demux(  mod->pmt
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
    mod->pmt->pid = psi->pid;

    ca_stream_t *ca_stream_g = NULL;
    bool is_ecm_selected;

    uint16_t skip = 12;
    memcpy(mod->pmt->buffer, psi->buffer, 10);

    is_ecm_selected = false;
    const uint8_t *desc_pointer;
    PMT_DESC_FOREACH(psi, desc_pointer)
    {
        if(desc_pointer[0] == 0x09)
        {
            ca_stream_t *__ca_stream = __pmt_check_desc(mod, desc_pointer, is_ecm_selected);
            if(__ca_stream)
            {
                ca_stream_g = __ca_stream;
                is_ecm_selected = true;
            }
        }
        else
        {
            const uint8_t size = desc_pointer[1] + 2;
            memcpy(&mod->pmt->buffer[skip], desc_pointer, size);
            skip += size;
        }
    }
    const uint16_t size = skip - 12; // 12 - PMT header
    mod->pmt->buffer[10] = (psi->buffer[10] & 0xF0) | ((size >> 8) & 0x0F);
    mod->pmt->buffer[11] = size & 0xFF;

    const uint8_t *pointer;
    PMT_ITEMS_FOREACH(psi, pointer)
    {
        memcpy(&mod->pmt->buffer[skip], pointer, 5);
        skip += 5;

        const uint16_t skip_last = skip;

        ca_stream_t *ca_stream_e = ca_stream_g;
        is_ecm_selected = (ca_stream_g != NULL);
        PMT_ITEM_DESC_FOREACH(pointer, desc_pointer)
        {
            if(desc_pointer[0] == 0x09)
            {
                ca_stream_t *__ca_stream = __pmt_check_desc(mod, desc_pointer, is_ecm_selected);
                if(__ca_stream)
                {
                    ca_stream_e = __ca_stream;
                    is_ecm_selected = true;
                }
            }
            else
            {
                const uint8_t size = desc_pointer[1] + 2;
                memcpy(&mod->pmt->buffer[skip], desc_pointer, size);
                skip += size;
            }
        }

        if(ca_stream_e)
        {
            el_stream_t *el_stream = malloc(sizeof(el_stream_t));
            el_stream->es_pid = PMT_ITEM_GET_PID(psi, pointer);
            el_stream->ca_stream = ca_stream_e;
            asc_list_insert_tail(mod->el_list, el_stream);
        }

        const uint16_t size = skip - skip_last;
        mod->pmt->buffer[skip_last - 2] = (size << 8) & 0x0F;
        mod->pmt->buffer[skip_last - 1] = size & 0xFF;
    }

    mod->pmt->buffer_size = skip + CRC32_SIZE;
    PSI_SET_SIZE(mod->pmt);
    PSI_SET_CRC32(mod->pmt);

    mpegts_psi_demux(  mod->pmt
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

    if(!mod->__decrypt.cam->is_ready)
        return;

    if(psi->buffer_size > EM_MAX_SIZE)
    {
        asc_log_error(MSG("em size is greater than %d"), EM_MAX_SIZE);
        return;
    }

    ca_stream_t *ca_stream = NULL;

    const uint8_t em_type = psi->buffer[0];

    if(em_type == 0x80 || em_type == 0x81)
    { /* ECM */
        asc_list_for(mod->ca_list)
        {
            ca_stream_t *i = asc_list_data(mod->ca_list);
            if(i->ecm_pid == psi->pid)
            {
                ca_stream = i;
                break;
            }
        }

        if(!ca_stream)
            return;

        if(em_type == ca_stream->ecm_type)
            return;

        if(!module_cas_check_em(mod->__decrypt.cas, psi))
            return;

        ca_stream->ecm_type = em_type;
        ca_stream->sendtime = asc_utime();
    }
    else if(em_type >= 0x82 && em_type <= 0x8F)
    { /* EMM */
        if(mod->disable_emm || mod->__decrypt.cam->disable_emm)
            return;

        if(!module_cas_check_em(mod->__decrypt.cas, psi))
            return;
    }
    else
    {
        asc_log_error(MSG("wrong packet type 0x%02X"), em_type);
        return;
    }

    mod->__decrypt.cam->send_em(  mod->__decrypt.cam->self
                                , &mod->__decrypt, ca_stream
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

static void decrypt(module_data_t *mod)
{
    asc_list_for(mod->ca_list)
    {
        ca_stream_t *ca_stream = asc_list_data(mod->ca_list);

        if(ca_stream->batch_skip > 0)
        {

#if FFDECSA == 1

            ca_stream->batch[ca_stream->batch_skip] = NULL;

            size_t i = 0, i_size = ca_stream->batch_skip / 2;
            while(i < i_size)
                i += decrypt_packets(ca_stream->keys, ca_stream->batch);

#elif LIBDVBCSA == 1

            ca_stream->batch[ca_stream->batch_skip].data = NULL;

            if(ca_stream->parity == 0x80)
                dvbcsa_bs_decrypt(ca_stream->even_key, ca_stream->batch, TS_BODY_SIZE);
            else if(ca_stream->parity == 0xC0)
                dvbcsa_bs_decrypt(ca_stream->odd_key, ca_stream->batch, TS_BODY_SIZE);

#endif

            ca_stream->batch_skip = 0;
        }

        // check new key
        switch(ca_stream->new_key_id)
        {
            case 0:
                break;
            case 1:
                ca_stream_set_keys(ca_stream, &ca_stream->new_key[0], NULL);
                ca_stream->new_key_id = 0;
                break;
            case 2:
                ca_stream_set_keys(ca_stream, NULL, &ca_stream->new_key[8]);
                ca_stream->new_key_id = 0;
                break;
            case 3:
                ca_stream_set_keys(  ca_stream
                                   , &ca_stream->new_key[0]
                                   , &ca_stream->new_key[8]);
                ca_stream->new_key_id = 0;
                break;
        }
    }

    mod->storage.dsc_count = mod->storage.count;
}

static void on_ts(module_data_t *mod, const uint8_t *ts)
{
    const uint16_t pid = TS_GET_PID(ts);

    if(pid == 0)
    {
        mpegts_psi_mux(mod->stream[pid], ts, on_pat, mod);
    }
    else if(pid == 1)
    {
        if(mod->stream[pid])
            mpegts_psi_mux(mod->stream[pid], ts, on_cat, mod);
        return;
    }
    else if(pid == NULL_TS_PID)
    {
        return;
    }
    else if(mod->stream[pid])
    {
        switch(mod->stream[pid]->type)
        {
            case MPEGTS_PACKET_PMT:
                mpegts_psi_mux(mod->stream[pid], ts, on_pmt, mod);
                return;
            case MPEGTS_PACKET_ECM:
            case MPEGTS_PACKET_EMM:
                mpegts_psi_mux(mod->stream[pid], ts, on_em, mod);
            case MPEGTS_PACKET_CA:
                return;
            default:
                break;
        }
    }

    if(asc_list_size(mod->ca_list) == 0)
    {
        module_stream_send(mod, ts);
        return;
    }

    if(mod->shift.buffer)
    {
        memcpy(&mod->shift.buffer[mod->shift.write], ts, TS_PACKET_SIZE);
        mod->shift.write += TS_PACKET_SIZE;
        if(mod->shift.write == mod->shift.size)
            mod->shift.write = 0;
        mod->shift.count += TS_PACKET_SIZE;

        if(mod->shift.count < mod->shift.size)
            return;

        ts = &mod->shift.buffer[mod->shift.read];
        mod->shift.read += TS_PACKET_SIZE;
        if(mod->shift.read == mod->shift.size)
            mod->shift.read = 0;
        mod->shift.count -= TS_PACKET_SIZE;
    }

    uint8_t *dst = &mod->storage.buffer[mod->storage.write];
    memcpy(dst, ts, TS_PACKET_SIZE);

    mod->storage.write += TS_PACKET_SIZE;
    if(mod->storage.write == mod->storage.size)
        mod->storage.write = 0;
    mod->storage.count += TS_PACKET_SIZE;

#if FFDECSA == 1

    asc_list_first(mod->ca_list);
    ca_stream_t *ca_stream = asc_list_data(mod->ca_list);

    ca_stream->batch[ca_stream->batch_skip    ] = dst;
    ca_stream->batch[ca_stream->batch_skip + 1] = dst + TS_PACKET_SIZE;
    ca_stream->batch_skip += 2;

    if(ca_stream->batch_skip >= mod->batch_size * 2)
        decrypt(mod);

#elif LIBDVBCSA == 1

    const uint8_t sc = TS_IS_SCRAMBLED(dst);
    if(sc)
    {
        dst[3] &= ~0xC0;

        int hdr_size = 0;

        if(TS_IS_PAYLOAD(ts))
        {
            if(TS_IS_AF(ts))
                hdr_size = 4 + dst[4] + 1;
            else
                hdr_size = 4;
        }

        if(hdr_size)
        {
            ca_stream_t *ca_stream = NULL;
            asc_list_for(mod->el_list)
            {
                el_stream_t *el_stream = asc_list_data(mod->el_list);
                if(el_stream->es_pid == pid)
                {
                    ca_stream = el_stream->ca_stream;
                    break;
                }
            }
            if(!ca_stream)
            {
                asc_list_first(mod->ca_list);
                ca_stream = asc_list_data(mod->ca_list);
            }

            if(ca_stream->parity != sc)
            {
                if(ca_stream->parity != 0x00)
                    decrypt(mod);
                ca_stream->parity = sc;
            }

            ca_stream->batch[ca_stream->batch_skip].data = &dst[hdr_size];
            ca_stream->batch[ca_stream->batch_skip].len = TS_PACKET_SIZE - hdr_size;
            ++ca_stream->batch_skip;

            if(ca_stream->batch_skip >= mod->batch_size)
                decrypt(mod);
        }
    }

#endif

    if(mod->storage.count >= mod->storage.size)
        decrypt(mod);

    if(mod->storage.dsc_count > 0)
    {
        module_stream_send(mod, &mod->storage.buffer[mod->storage.read]);
        mod->storage.read += TS_PACKET_SIZE;
        if(mod->storage.read == mod->storage.size)
            mod->storage.read = 0;
        mod->storage.dsc_count -= TS_PACKET_SIZE;
        mod->storage.count -= TS_PACKET_SIZE;
    }
}

/*
 *      o      oooooooooo ooooo
 *     888      888    888 888
 *    8  88     888oooo88  888
 *   8oooo88    888        888
 * o88o  o888o o888o      o888o
 *
 */

void on_cam_ready(module_data_t *mod)
{
    mod->caid = mod->__decrypt.cam->caid;

    stream_reload(mod);
}

void on_cam_error(module_data_t *mod)
{
    mod->caid = 0x0000;

    module_decrypt_cas_destroy(mod);
}

void on_cam_response(module_data_t *mod, void *arg, const uint8_t *data)
{
    ca_stream_t *ca_stream = arg;
    asc_list_for(mod->ca_list)
    {
        if(asc_list_data(mod->ca_list) == ca_stream)
            break;
    }
    if(asc_list_eol(mod->ca_list))
        return;

    if((data[0] & ~0x01) != 0x80)
        return; /* Skip EMM */

    if(!mod->__decrypt.cas)
        return; /* after stream_reload */

    bool is_keys_ok = false;
    do
    {
        if(!module_cas_check_keys(mod->__decrypt.cas, data))
            break;

        if(data[2] != 16)
            break;

        const uint8_t ck1 = (data[3] + data[4] + data[5]) & 0xFF;
        if(ck1 != data[6])
            break;

        const uint8_t ck2 = (data[7] + data[8] + data[9]) & 0xFF;
        if(ck2 != data[10])
            break;

        is_keys_ok = true;
    } while(0);

    if(is_keys_ok)
    {
        // Set keys
        if(ca_stream->new_key[11] == data[14] && ca_stream->new_key[15] == data[18])
        {
            ca_stream->new_key_id = 1;
            memcpy(&ca_stream->new_key[0], &data[3], 8);
        }
        else if(ca_stream->new_key[3] == data[6] && ca_stream->new_key[7] == data[10])
        {
            ca_stream->new_key_id = 2;
            memcpy(&ca_stream->new_key[8], &data[11], 8);
        }
        else
        {
            ca_stream->new_key_id = 3;
            memcpy(ca_stream->new_key, &data[3], 16);
            if(ca_stream->is_keys)
                asc_log_warning(MSG("Both keys changed"));
            else
                ca_stream->is_keys = true;
        }

        if(asc_log_is_debug())
        {
            char key_1[17], key_2[17];
            hex_to_str(key_1, &data[3], 8);
            hex_to_str(key_2, &data[11], 8);
            const uint64_t responsetime = (asc_utime() - ca_stream->sendtime) / 1000;
            asc_log_debug(  MSG("ECM Found id:0x%02X time:%llums key:%s:%s")
                          , data[0], responsetime, key_1, key_2);
        }

    }
    else
    {
        const uint64_t responsetime = (asc_utime() - ca_stream->sendtime) / 1000;
        asc_log_error(  MSG("ECM Not Found id:0x%02X time:%llums size:%d")
                      , data[0], responsetime, data[2]);
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

    mod->__decrypt.self = mod;

    module_option_string("name", &mod->name, NULL);
    asc_assert(mod->name != NULL, "[decrypt] option 'name' is required");

    mod->stream[0] = mpegts_psi_init(MPEGTS_PACKET_PAT, 0);
    mod->pmt = mpegts_psi_init(MPEGTS_PACKET_PMT, MAX_PID);

    mod->ca_list = asc_list_init();
    mod->el_list = asc_list_init();

#if FFDECSA == 1

    mod->batch_size = get_suggested_cluster_size();

#elif LIBDVBCSA == 1

    mod->batch_size = dvbcsa_bs_batch_size();

#endif

    mod->storage.size = mod->batch_size * 4 * TS_PACKET_SIZE;
    mod->storage.buffer = malloc(mod->storage.size);

    const char *biss_key = NULL;
    size_t biss_length = 0;
    module_option_string("biss", &biss_key, &biss_length);
    if(biss_key)
    {
        asc_assert(biss_length == 16, MSG("biss key must be 16 char length"));

        mod->caid = BISS_CAID;
        mod->disable_emm = true;

        uint8_t key[8];
        str_to_hex(biss_key, key, sizeof(key));
        key[3] = (key[0] + key[1] + key[2]) & 0xFF;
        key[7] = (key[4] + key[5] + key[6]) & 0xFF;

        ca_stream_t *biss = ca_stream_init(mod, NULL_TS_PID);
        ca_stream_set_keys(biss, key, key);
    }

    lua_getfield(lua, 2, "cam");
    if(!lua_isnil(lua, -1))
    {
        asc_assert(  lua_type(lua, -1) == LUA_TLIGHTUSERDATA
                   , "option 'cam' required cam-module instance");
        mod->__decrypt.cam = lua_touserdata(lua, -1);

        int cas_pnr = 0;
        module_option_number("cas_pnr", &cas_pnr);
        if(cas_pnr > 0 && cas_pnr <= 0xFFFF)
            mod->__decrypt.cas_pnr = (uint16_t)cas_pnr;

        const char *cas_data = NULL;
        module_option_string("cas_data", &cas_data, NULL);
        if(cas_data)
        {
            mod->__decrypt.is_cas_data = true;
            str_to_hex(cas_data, mod->__decrypt.cas_data, sizeof(mod->__decrypt.cas_data));
        }

        module_option_boolean("disable_emm", &mod->disable_emm);
        module_option_number("ecm_pid", &mod->ecm_pid);

        module_cam_attach_decrypt(mod->__decrypt.cam, &mod->__decrypt);
    }
    lua_pop(lua, 1);

    int shift = 0;
    module_option_number("shift", &shift);
    if(shift > 0)
    {
        mod->shift.size = (shift * 1000 * 1000) / (TS_PACKET_SIZE * 8) * (TS_PACKET_SIZE);
        mod->shift.buffer = malloc(mod->shift.size);
    }

    stream_reload(mod);
}

static void module_destroy(module_data_t *mod)
{
    module_stream_destroy(mod);

    if(mod->__decrypt.cam)
    {
        module_cam_detach_decrypt(mod->__decrypt.cam, &mod->__decrypt);
        mod->__decrypt.cam = NULL;
    }

    module_decrypt_cas_destroy(mod);

    if(mod->caid == BISS_CAID)
    {
        asc_list_first(mod->ca_list);
        ca_stream_t *ca_stream = asc_list_data(mod->ca_list);
        ca_stream_destroy(ca_stream);
        asc_list_remove_current(mod->ca_list);
    }

    asc_list_destroy(mod->ca_list);
    asc_list_destroy(mod->el_list);

    free(mod->storage.buffer);

    if(mod->shift.buffer)
        free(mod->shift.buffer);

    for(int i = 0; i < MAX_PID; ++i)
    {
        if(mod->stream[i])
        {
            mpegts_psi_destroy(mod->stream[i]);
            mod->stream[i] = NULL;
        }
    }
    mpegts_psi_destroy(mod->pmt);
}

MODULE_STREAM_METHODS()
MODULE_LUA_METHODS()
{
    MODULE_STREAM_METHODS_REF()
};
MODULE_LUA_REGISTER(decrypt)
