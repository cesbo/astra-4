/*
 * Astra Module Demux API
 * http://cesbo.com/astra
 *
 * Copyright (C) 2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

/*
 * Module Name:
 *      channel
 *
 * Module Options:
 *      upstream    - object, stream instance returned by module_instance:stream()
 *      demux       - object, demux instance returned by module_instance:demux()
 *      pnr         - number, join PID related to the program number
 *      caid        - number, CAID to join CAS PID (CAT,EMM,ECM)
 */

#include <astra.h>

struct module_data_t
{
    MODULE_STREAM_DATA();
    MODULE_DEMUX_DATA();

    char *name;
    int pnr;
    int caid;

    mpegts_psi_t *stream[MAX_PID];
};

#define MSG(_msg) "[analyze %s] " _msg, mod->name

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
        log_error(MSG("PAT checksum mismatch"));
        return;
    }
    psi->crc32 = crc32;

    const uint8_t *pointer = PAT_ITEMS_FIRST(psi);
    while(!PAT_ITEMS_EOF(psi, pointer))
    {
        const uint16_t pnr = PAT_ITEMS_GET_PNR(psi, pointer);
        const uint16_t pid = PAT_ITEMS_GET_PID(psi, pointer);
        if(pnr)
        {
            demux_join_pid(mod, pid);
            mod->stream[pid] = mpegts_psi_init(MPEGTS_PACKET_PMT, pid);
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
        log_error(MSG("PMT checksum mismatch"));
        return;
    }
    psi->crc32 = crc32;

    const uint8_t *desc, *desc_pointer;
    desc = CAT_GET_DESC(psi);
    desc_pointer = DESC_ITEMS_FIRST(desc);
    while(!DESC_ITEMS_EOF(desc, desc_pointer))
    {
        if(desc_pointer[0] == 0x09 && DESC_CA_CAID(desc_pointer) == mod->caid)
            demux_join_pid(mod, DESC_CA_PID(desc_pointer));

        DESC_ITEMS_NEXT(desc, desc_pointer);
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
    if(pnr != mod->pnr)
        return;

    // check changes
    const uint32_t crc32 = PSI_GET_CRC32(psi);
    if(crc32 == psi->crc32)
        return;

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
    {
        log_error(MSG("PMT checksum mismatch"));
        return;
    }
    psi->crc32 = crc32;

    const uint8_t *desc, *desc_pointer;
    if(mod->caid)
    {
        desc = PMT_GET_DESC(psi);
        desc_pointer = DESC_ITEMS_FIRST(desc);
        while(!DESC_ITEMS_EOF(desc, desc_pointer))
        {
            if(desc_pointer[0] == 0x09 && DESC_CA_CAID(desc_pointer) == mod->caid)
                demux_join_pid(mod, DESC_CA_PID(desc_pointer));

            DESC_ITEMS_NEXT(desc, desc_pointer);
        }
    }

    const uint8_t *pointer = PMT_ITEMS_FIRST(psi);
    while(!PMT_ITEMS_EOF(psi, pointer))
    {
        const uint16_t pid = PMT_ITEMS_GET_PID(psi, pointer);

        if(mod->caid)
        {
            desc = PMT_ITEMS_GET_DESC(psi, pointer);
            desc_pointer = DESC_ITEMS_FIRST(desc);
            while(!DESC_ITEMS_EOF(desc, desc_pointer))
            {
                if(desc_pointer[0] == 0x09 && DESC_CA_CAID(desc_pointer) == mod->caid)
                    demux_join_pid(mod, DESC_CA_PID(desc_pointer));

                DESC_ITEMS_NEXT(desc, desc_pointer);
            }
        }

        demux_join_pid(mod, pid);

        PMT_ITEMS_NEXT(psi, pointer);
    }
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
    if(!demux_is_pid(mod, pid))
        return;

    mpegts_psi_t *psi = mod->stream[pid];
    if(psi)
    {
        switch(psi->type)
        {
            case MPEGTS_PACKET_PAT:
                mpegts_psi_mux(psi, ts, on_pat, mod);
                break;
            case MPEGTS_PACKET_CAT:
                mpegts_psi_mux(psi, ts, on_cat, mod);
                break;
            case MPEGTS_PACKET_PMT:
                mpegts_psi_mux(psi, ts, on_pmt, mod);
                break;
            default:
                break;
        }
    }

    module_stream_send(mod, ts);
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
    module_demux_init(mod, NULL, NULL);

    const char *string_value = NULL;

    const int name_length = module_option_string("name", &string_value);
    if(!string_value)
    {
        log_error("[channel] option 'name' is required");
        astra_abort();
    }
    mod->name = malloc(name_length + 1);
    strcpy(mod->name, string_value);

    lua_getfield(lua, MODULE_OPTIONS_IDX, "demux");
    if(lua_type(lua, -1) != LUA_TLIGHTUSERDATA)
    {
        log_error("[channel] option 'demux' is required");
        astra_abort();
    }
    demux_set_parent(mod, lua_touserdata(lua, -1));
    lua_pop(lua, 1);

    if(module_option_number("pnr", &mod->pnr))
    {
        mod->stream[0] = mpegts_psi_init(MPEGTS_PACKET_PAT, 0);
        demux_join_pid(mod, 0);
    }

    if(module_option_number("caid", &mod->caid) && mod->caid == 1)
    {
        mod->stream[1] = mpegts_psi_init(MPEGTS_PACKET_CAT, 1);
        demux_join_pid(mod, 1);
    }

    // TODO: parse options
}

static void module_destroy(module_data_t *mod)
{
    module_stream_destroy(mod);
    module_demux_destroy(mod);

    free(mod->name);
}

MODULE_STREAM_METHODS()
MODULE_LUA_METHODS()
{
    MODULE_STREAM_METHODS_REF()
};
MODULE_LUA_REGISTER(channel)
