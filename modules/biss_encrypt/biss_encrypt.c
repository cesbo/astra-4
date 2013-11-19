/*
 * Astra Module: BISS Encrypt
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
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

#include <astra.h>
#include "dvbcsa/dvbcsa.h"

struct module_data_t
{
    MODULE_LUA_DATA();
    MODULE_STREAM_DATA();

    mpegts_packet_type_t stream[MAX_PID];

    mpegts_psi_t *pat;
    mpegts_psi_t *pmt;

    struct dvbcsa_bs_key_s *key;

    size_t storage_size;
    size_t storage_skip;

    int batch_skip;
    uint8_t *batch_storage_recv;
    uint8_t *batch_storage_send;
    struct dvbcsa_bs_batch_s *batch;
};

static void process_ts(module_data_t *mod, const uint8_t *ts, uint8_t hdr_size)
{
    uint8_t *dst = &mod->batch_storage_recv[mod->storage_skip];
    memcpy(dst, ts, TS_PACKET_SIZE);

    if(hdr_size)
    {
        dst[3] |= 0x80;
        mod->batch[mod->batch_skip].data = &dst[hdr_size];
        mod->batch[mod->batch_skip].len = TS_PACKET_SIZE - hdr_size;
        ++mod->batch_skip;
    }

    if(mod->batch_storage_send)
        module_stream_send(mod, &mod->batch_storage_send[mod->storage_skip]);

    mod->storage_skip += TS_PACKET_SIZE;

    if(mod->storage_skip >= mod->storage_size)
    {
        mod->batch[mod->batch_skip].data = NULL;
        dvbcsa_bs_encrypt(mod->key, mod->batch, 184);
        uint8_t *storage_tmp = mod->batch_storage_send;
        mod->batch_storage_send = mod->batch_storage_recv;
        if(!storage_tmp)
            storage_tmp = malloc(mod->storage_size);
        mod->batch_storage_recv = storage_tmp;
        mod->batch_skip = 0;
        mod->storage_skip = 0;
    }
}

static void on_pat(void *arg, mpegts_psi_t *psi)
{
    module_data_t *mod = arg;

    // check changes
    const uint32_t crc32 = PSI_GET_CRC32(psi);
    if(crc32 == psi->crc32)
        return;

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
        return; // PAT checksum error

    psi->crc32 = crc32;

    memset(mod->stream, 0, sizeof(mod->stream));
    mod->stream[0] = MPEGTS_PACKET_PAT;
    mod->pmt->crc32 = 0;

    const uint8_t *pointer = PAT_ITEMS_FIRST(psi);
    while(!PAT_ITEMS_EOL(psi, pointer))
    {
        const uint16_t pnr = PAT_ITEMS_GET_PNR(psi, pointer);
        const uint16_t pid = PAT_ITEMS_GET_PID(psi, pointer);
        mod->stream[pid] = (pnr) ? MPEGTS_PACKET_PMT : MPEGTS_PACKET_NIT;
        PAT_ITEMS_NEXT(psi, pointer);
    }
}

static void on_pmt(void *arg, mpegts_psi_t *psi)
{
    module_data_t *mod = arg;

    // check changes
    const uint32_t crc32 = PSI_GET_CRC32(psi);
    if(crc32 == psi->crc32)
        return;

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
        return; // PAT checksum error

    psi->crc32 = crc32;

    const uint8_t *pointer = PMT_ITEMS_FIRST(psi);
    while(!PMT_ITEMS_EOL(psi, pointer))
    {
        const uint16_t pid = PMT_ITEM_GET_PID(psi, pointer);
        mod->stream[pid] = MPEGTS_PACKET_PES;
        PMT_ITEMS_NEXT(psi, pointer);
    }
}

static void on_ts(module_data_t *mod, const uint8_t *ts)
{
    const uint16_t pid = TS_PID(ts);
    switch(mod->stream[pid])
    {
        case MPEGTS_PACKET_PES:
            break;
        case MPEGTS_PACKET_PAT:
            mpegts_psi_mux(mod->pat, ts, on_pat, mod);
            process_ts(mod, ts, 0);
            return;
        case MPEGTS_PACKET_PMT:
            mpegts_psi_mux(mod->pmt, ts, on_pmt, mod);
            process_ts(mod, ts, 0);
            return;
        default:
            process_ts(mod, ts, 0);
            return;
    }

    uint8_t hdr_size = 4;
    switch(TS_AF(ts))
    {
        case 0x10:
            break;
        case 0x30:
        {
            hdr_size += ts[4] + 1;
            if(hdr_size < TS_PACKET_SIZE)
                break;
        }
        default:
            process_ts(mod, ts, 0);
            return;
    }

    process_ts(mod, ts, hdr_size);
}

static void module_init(module_data_t *mod)
{
    module_stream_init(mod, on_ts);


    const char *key_value = NULL;
    module_option_string("key", &key_value);
    asc_assert(key_value != NULL, "[biss_encrypt] option 'key' is required");
    asc_assert(strlen(key_value) == 16, "[biss_encrypt] key must be 16 char length");

    uint8_t key[8];
    str_to_hex(key_value, key, 16);
    key[3] = (key[0] + key[1] + key[2]) & 0xFF;
    key[7] = (key[4] + key[5] + key[6]) & 0xFF;

    const int batch_size = dvbcsa_bs_batch_size();
    mod->batch = calloc(batch_size + 1, sizeof(struct dvbcsa_bs_batch_s));
    mod->storage_size = batch_size * TS_PACKET_SIZE;
    mod->batch_storage_recv = malloc(mod->storage_size);

    mod->key = dvbcsa_bs_key_alloc();
    dvbcsa_bs_key_set(key, mod->key);

    mod->stream[0x00] = MPEGTS_PACKET_PAT;
    mod->pat = mpegts_psi_init(MPEGTS_PACKET_PAT, 0);
    mod->pmt = mpegts_psi_init(MPEGTS_PACKET_PMT, 0);
}

static void module_destroy(module_data_t *mod)
{
    module_stream_destroy(mod);

    dvbcsa_bs_key_free(mod->key);

    mpegts_psi_destroy(mod->pat);
    mpegts_psi_destroy(mod->pmt);
}

MODULE_STREAM_METHODS()
MODULE_LUA_METHODS()
{
    MODULE_STREAM_METHODS_REF()
};

MODULE_LUA_REGISTER(biss_encrypt)
