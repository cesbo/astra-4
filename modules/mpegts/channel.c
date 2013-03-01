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
 *      cas         - boolean, join CAS PID (CAT,EMM,ECM)
 */

/*
 * input_instance = input_module({ ... })
 * channel_instance = channel({ demux = input_instance:demux(), ... })
 * input_instance:attach(channel_instance) -- TODO: remove that, attach while channel init
 */

#include <astra.h>

struct module_data_t
{
    MODULE_STREAM_DATA();
    MODULE_DEMUX_DATA();

    char *name;
    int pnr;
    int cas;

    mpegts_psi_t *stream[MAX_PID];

    mpegts_psi_t *custom_pat, *custom_pmt;
};

#define MSG(_msg) "[analyze %s] " _msg, mod->name

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
    if(mod->cas)
    {
        desc = PMT_GET_DESC(psi);
        desc_pointer = DESC_ITEMS_FIRST(desc);
        while(!DESC_ITEMS_EOF(desc, desc_pointer))
        {
            // TODO: join ECM
            DESC_ITEMS_NEXT(desc, desc_pointer);
        }
    }

    const uint8_t *pointer = PMT_ITEMS_FIRST(psi);
    while(!PMT_ITEMS_EOF(psi, pointer))
    {
        const uint16_t pid = PMT_ITEMS_GET_PID(psi, pointer);
        // const uint8_t type = PMT_ITEMS_GET_TYPE(psi, pointer);

        if(mod->cas)
        {
            desc = PMT_ITEMS_GET_DESC(psi, pointer);
            desc_pointer = DESC_ITEMS_FIRST(desc);
            while(!DESC_ITEMS_EOF(desc, desc_pointer))
            {
                // TODO: join ECM
                DESC_ITEMS_NEXT(desc, desc_pointer);
            }
        }

        demux_join_pid(mod, pid);

        PMT_ITEMS_NEXT(psi, pointer);
    }
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
        log_error(MSG("PMT checksum mismatch"));
        return;
    }
    psi->crc32 = crc32;

    const uint8_t *desc, *desc_pointer;
    desc = CAT_GET_DESC(psi);
    desc_pointer = DESC_ITEMS_FIRST(desc);
    while(!DESC_ITEMS_EOF(desc, desc_pointer))
    {
        // TODO: join EMM
        DESC_ITEMS_NEXT(desc, desc_pointer);
    }
}

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
                // return;
            case MPEGTS_PACKET_CAT:
                mpegts_psi_mux(psi, ts, on_cat, mod);
                break;
                // return;
            case MPEGTS_PACKET_PMT:
                mpegts_psi_mux(psi, ts, on_pmt, mod);
                break;
                // return;
            default:
                break;
        }
    }

    module_stream_send(mod, ts);
}

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

    if(module_option_number("cas", &mod->cas) && mod->cas == 1)
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
