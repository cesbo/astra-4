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
 *      biss        - string, BISS key, 16 chars length. example: biss = "1122330044556600"
 *      cam         - object, cam instance returned by cam_module_instance:cam()
 */

#include <astra.h>
#include "module_cam.h"
#include "FFdecsa/FFdecsa.h"

struct module_data_t
{
    MODULE_LUA_DATA();
    MODULE_STREAM_DATA();

    /* Config */
    const char *name;

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

    /* Base */
    mpegts_psi_t *pat;
    mpegts_psi_t *cat;
    mpegts_psi_t *pmt;
    mpegts_psi_t *custom_pmt;

    int pmt_pnr;
    int pmt_pid;

    cam_t *cam;
};

#define MSG(_msg) "[decrypt %s] " _msg, mod->name

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
    psi->crc32 = crc32;

    const uint8_t *pointer = PAT_ITEMS_FIRST(psi);
    while(!PAT_ITEMS_EOL(psi, pointer))
    {
        const uint16_t pnr = PAT_ITEMS_GET_PNR(psi, pointer);
        if(pnr)
        {
            if(mod->pmt_pid)
            {
                asc_log_error(MSG("only SPTS allowed"));
                astra_abort();
            }
            mod->pmt_pnr = pnr;
            mod->pmt_pid = PAT_ITEMS_GET_PID(psi, pointer);
            mod->pmt = mpegts_psi_init(MPEGTS_PACKET_PMT, mod->pmt_pid);
            mod->custom_pmt = mpegts_psi_init(MPEGTS_PACKET_PMT, mod->pmt_pid);
        }
        PAT_ITEMS_NEXT(psi, pointer);
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
    psi->crc32 = crc32;

    // TODO: send to cas
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
    if(pnr != mod->pmt_pnr)
        return;

    // check changes
    const uint32_t crc32 = PSI_GET_CRC32(psi);
    if(crc32 == psi->crc32 && mod->custom_pmt->buffer_size > 0)
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
    psi->crc32 = crc32;

    // TODO: send to cas

    memcpy(mod->custom_pmt->buffer, psi->buffer, psi->buffer_size);
    mod->custom_pmt->buffer_size = psi->buffer_size;
    // TODO: build custom_pat
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

    if(pid == 0)
        mpegts_psi_mux(mod->pat, ts, on_pat, mod);
    else if(pid == 1)
        mpegts_psi_mux(mod->cat, ts, on_cat, mod);
    else if(pid == mod->pmt_pid)
        mpegts_psi_mux(mod->pmt, ts, on_pmt, mod);

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

    // TODO: check is new key

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
    }
    set_control_words(mod->ffdecsa, first_key, first_key);

    lua_getfield(lua, 2, "cam");
    if(!lua_isnil(lua, -1))
    {
        if(lua_type(lua, -1) != LUA_TLIGHTUSERDATA)
        {
            asc_log_error(MSG("option 'cam' required cam-module instance"));
            astra_abort();
        }
        mod->cam = lua_touserdata(lua, -1);
    }
    lua_pop(lua, 1); // cam

    mod->buffer = malloc(mod->cluster_size_bytes * 2);
    mod->r_buffer = mod->buffer; // s_buffer = NULL

    mod->pat = mpegts_psi_init(MPEGTS_PACKET_PAT, 0);
    mod->cat = mpegts_psi_init(MPEGTS_PACKET_CAT, 1);
}

static void module_destroy(module_data_t *mod)
{
    module_stream_destroy(mod);

    free_key_struct(mod->ffdecsa);
    free(mod->cluster);
    free(mod->buffer);

    mpegts_psi_destroy(mod->pat);
    mpegts_psi_destroy(mod->cat);
    if(mod->pmt)
    {
        mpegts_psi_destroy(mod->pmt);
        mpegts_psi_destroy(mod->custom_pmt);
    }
}

MODULE_STREAM_METHODS()
MODULE_LUA_METHODS()
{
    MODULE_STREAM_METHODS_REF()
};
MODULE_LUA_REGISTER(decrypt)
