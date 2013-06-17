/*
 * Astra MPEG-TS Analyze Module
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

/*
 * Module Name:
 *      analyze
 *
 * Module Options:
 *      upstream    - object, stream instance returned by module_instance:stream()
 *      name        - string, analyzer name
 *      on_psi      - function,
 *      on_error    - function,
 */

#include <astra.h>

struct module_data_t
{
    MODULE_LUA_DATA();
    MODULE_STREAM_DATA();

    const char *name;

    uint16_t tsid;

    int stream_ndx;
    mpegts_psi_t *stream[MAX_PID];
};

#define MSG(_msg) "[analyze %s] " _msg, mod->name

static const char __type[] = "type";
static const char __pid[] = "pid";
static const char __crc32[] = "crc32";
static const char __pnr[] = "pnr";
static const char __tsid[] = "tsid";
static const char __descriptors[] = "descriptors";
static const char __checksum_error[] = "checksum error";

static void on_error_call(module_data_t *mod, const char *msg)
{
    asc_assert((lua_type(lua, -1) == LUA_TTABLE), "table required");

    lua_pushstring(lua, msg);
    lua_setfield(lua, -2, "error");

    lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->stream_ndx);
    const int len = luaL_len(lua, -1);
    lua_pushnumber(lua, len + 1);
    lua_pushvalue(lua, -3);
    lua_settable(lua, -3);
    lua_pop(lua, 1); // stream

    lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->__lua.oref);
    lua_getfield(lua, -1, "on_error");
    lua_remove(lua, -2);

    if(!lua_isnil(lua, -1))
    {
        lua_pushvalue(lua, -2);
        lua_call(lua, 1, 0);
    }
    else
        lua_pop(lua, 1); // on_error
}

static void on_psi_call(module_data_t *mod)
{
    asc_assert((lua_type(lua, -1) == LUA_TTABLE), "table required");

    lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->stream_ndx);
    const int len = luaL_len(lua, -1);
    lua_pushnumber(lua, len + 1);
    lua_pushvalue(lua, -3);
    lua_settable(lua, -3);
    lua_pop(lua, 1); // stream

    lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->__lua.oref);
    lua_getfield(lua, -1, "on_psi");
    lua_remove(lua, -2);

    if(lua_isfunction(lua, -1))
    {
        lua_pushvalue(lua, -2); // push stream info
        lua_call(lua, 1, 0);
    }
    else
        lua_pop(lua, 1); // on_psi
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

    lua_newtable(lua);
    lua_pushstring(lua, "PAT");
    lua_setfield(lua, -2, __type);

    lua_pushnumber(lua, psi->pid);
    lua_setfield(lua, -2, __pid);

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
    {
        on_error_call(mod, __checksum_error);
        lua_pop(lua, 1); // info
        return;
    }

    psi->crc32 = crc32;
    mod->tsid = PAT_GET_TSID(psi);

    lua_pushnumber(lua, psi->crc32);
    lua_setfield(lua, -2, __crc32);

    lua_pushnumber(lua, mod->tsid);
    lua_setfield(lua, -2, __tsid);

    int programs_count = 1;
    lua_newtable(lua);
    const uint8_t *pointer = PAT_ITEMS_FIRST(psi);
    while(!PAT_ITEMS_EOL(psi, pointer))
    {
        const uint16_t pnr = PAT_ITEMS_GET_PNR(psi, pointer);
        const uint16_t pid = PAT_ITEMS_GET_PID(psi, pointer);

        lua_pushnumber(lua, programs_count++);
        lua_newtable(lua);
        lua_pushnumber(lua, pnr);
        lua_setfield(lua, -2, __pnr);
        lua_pushnumber(lua, pid);
        lua_setfield(lua, -2, __pid);
        lua_settable(lua, -3); // append to the "programs" table

        if(pnr)
            mod->stream[pid] = mpegts_psi_init(MPEGTS_PACKET_PMT, pid);

        PAT_ITEMS_NEXT(psi, pointer);
    }
    lua_setfield(lua, -2, "programs");

    on_psi_call(mod);
    lua_pop(lua, 1); // info
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

    lua_newtable(lua);
    lua_pushstring(lua, "CAT");
    lua_setfield(lua, -2, __type);

    lua_pushnumber(lua, psi->pid);
    lua_setfield(lua, -2, __pid);

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
    {
        on_error_call(mod, __checksum_error);
        lua_pop(lua, 1); // info
        return;
    }
    psi->crc32 = crc32;

    lua_pushnumber(lua, psi->crc32);
    lua_setfield(lua, -2, __crc32);

    int descriptors_count = 1;
    lua_newtable(lua);
    const uint8_t *desc_pointer = CAT_DESC_FIRST(psi);
    while(!CAT_DESC_EOL(psi, desc_pointer))
    {
        lua_pushnumber(lua, descriptors_count++);
        mpegts_desc_to_lua(desc_pointer);
        lua_settable(lua, -3); // append to the "descriptors" table

        CAT_DESC_NEXT(psi, desc_pointer);
    }
    lua_setfield(lua, -2, __descriptors);

    on_psi_call(mod);
    lua_pop(lua, 1); // info
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

    // check changes
    const uint32_t crc32 = PSI_GET_CRC32(psi);
    if(crc32 == psi->crc32)
        return;

    lua_newtable(lua);
    lua_pushstring(lua, "PMT");
    lua_setfield(lua, -2, __type);

    lua_pushnumber(lua, psi->pid);
    lua_setfield(lua, -2, __pid);

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
    {
        on_error_call(mod, __checksum_error);
        lua_pop(lua, 1); // info
        return;
    }
    psi->crc32 = crc32;

    lua_pushnumber(lua, psi->crc32);
    lua_setfield(lua, -2, __crc32);

    const uint16_t pnr = PMT_GET_PNR(psi);
    lua_pushnumber(lua, pnr);
    lua_setfield(lua, -2, __pnr);

    int descriptors_count = 1;
    lua_newtable(lua);
    const uint8_t *desc_pointer = PMT_DESC_FIRST(psi);
    while(!PMT_DESC_EOL(psi, desc_pointer))
    {
        lua_pushnumber(lua, descriptors_count++);
        mpegts_desc_to_lua(desc_pointer);
        lua_settable(lua, -3); // append to the "descriptors" table

        PMT_DESC_NEXT(psi, desc_pointer);
    }
    lua_setfield(lua, -2, __descriptors);

    lua_pushnumber(lua, PMT_GET_PCR(psi));
    lua_setfield(lua, -2, "pcr");

    int streams_count = 1;
    lua_newtable(lua);
    const uint8_t *pointer = PMT_ITEMS_FIRST(psi);
    while(!PMT_ITEMS_EOL(psi, pointer))
    {
        const uint16_t pid = PMT_ITEM_GET_PID(psi, pointer);
        const uint8_t type = PMT_ITEM_GET_TYPE(psi, pointer);

        lua_pushnumber(lua, streams_count++);
        lua_newtable(lua);

        lua_pushstring(lua, mpegts_type_name(mpegts_pes_type(type)));
        lua_setfield(lua, -2, "type_name");

        lua_pushnumber(lua, type);
        lua_setfield(lua, -2, "type_id");

        lua_pushnumber(lua, pid);
        lua_setfield(lua, -2, __pid);

        descriptors_count = 1;
        lua_newtable(lua);
        const uint8_t *desc_pointer = PMT_ITEM_DESC_FIRST(pointer);
        while(!PMT_ITEM_DESC_EOL(pointer, desc_pointer))
        {
            lua_pushnumber(lua, descriptors_count++);
            mpegts_desc_to_lua(desc_pointer);
            lua_settable(lua, -3); // append to the "streams[X].descriptors" table

            PMT_ITEM_DESC_NEXT(pointer, desc_pointer);
        }
        lua_setfield(lua, -2, __descriptors);

        lua_settable(lua, -3); // append to the "streams" table

        PMT_ITEMS_NEXT(psi, pointer);
    }
    lua_setfield(lua, -2, "streams");

    on_psi_call(mod);
    lua_pop(lua, 1); // options
}

/*
 *  oooooooo8 ooooooooo   ooooooooooo
 * 888         888    88o 88  888  88
 *  888oooooo  888    888     888
 *         888 888    888     888
 * o88oooo888 o888ooo88      o888o
 *
 */

static void on_sdt(void *arg, mpegts_psi_t *psi)
{
    module_data_t *mod = arg;

    if(psi->buffer[0] != 0x42)
        return;

    if(mod->tsid != SDT_GET_TSID(psi))
        return;

    // check changes
    const uint32_t crc32 = PSI_GET_CRC32(psi);
    if(crc32 == psi->crc32)
        return;

    lua_newtable(lua);
    lua_pushstring(lua, "SDT");
    lua_setfield(lua, -2, __type);

    lua_pushnumber(lua, psi->pid);
    lua_setfield(lua, -2, __pid);

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
    {
        on_error_call(mod, __checksum_error);
        lua_pop(lua, 1); // info
        return;
    }
    psi->crc32 = crc32;

    lua_pushnumber(lua, psi->crc32);
    lua_setfield(lua, -2, __crc32);

    lua_pushnumber(lua, mod->tsid);
    lua_setfield(lua, -2, __tsid);

    int descriptors_count;
    int services_count = 1;
    lua_newtable(lua);
    const uint8_t *pointer = SDT_ITEMS_FIRST(psi);
    while(!SDT_ITEMS_EOL(psi, pointer))
    {
        const uint16_t sid = SDT_ITEM_GET_SID(psi, pointer);

        lua_pushnumber(lua, services_count++);

        lua_newtable(lua);
        lua_pushnumber(lua, sid);
        lua_setfield(lua, -2, "sid");

        descriptors_count = 1;
        lua_newtable(lua);
        const uint8_t *desc_pointer = SDT_ITEM_DESC_FIRST(pointer);
        while(!SDT_ITEM_DESC_EOL(pointer, desc_pointer))
        {
            lua_pushnumber(lua, descriptors_count++);
            lua_pushlstring(lua, (const char *)desc_pointer, 2 + desc_pointer[1]);
            lua_settable(lua, -3);

            SDT_ITEM_DESC_NEXT(pointer, desc_pointer);
        }

        lua_settable(lua, -3); // append to the "services[X].descriptors" table

        SDT_ITEMS_NEXT(psi, pointer);
    }
    lua_setfield(lua, -2, "services");

    on_psi_call(mod);
    lua_pop(lua, 1); // options
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
            case MPEGTS_PACKET_SDT:
                mpegts_psi_mux(psi, ts, on_sdt, mod);
                break;
            default:
                break;
        }
    }
}

static int method_stream_info(module_data_t *mod)
{
    __uarg(mod);

    lua_newtable(lua);

    return 1;
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
    module_option_string("name", &mod->name);
    if(!mod->name)
    {
        asc_log_error("[analyze] option 'name' is required");
        astra_abort();
    }

    module_stream_init(mod, on_ts);

    mod->stream[0x00] = mpegts_psi_init(MPEGTS_PACKET_PAT, 0x00);
    mod->stream[0x01] = mpegts_psi_init(MPEGTS_PACKET_CAT, 0x01);
    mod->stream[0x11] = mpegts_psi_init(MPEGTS_PACKET_SDT, 0x11);

    lua_newtable(lua);
    mod->stream_ndx = luaL_ref(lua, LUA_REGISTRYINDEX);
}

static void module_destroy(module_data_t *mod)
{
    module_stream_destroy(mod);

    for(int i = 0; i < MAX_PID; ++i)
    {
        if(mod->stream[i])
            mpegts_psi_destroy(mod->stream[i]);
    }

    if(mod->stream_ndx)
    {
        luaL_unref(lua, LUA_REGISTRYINDEX, mod->stream_ndx);
        mod->stream_ndx = 0;
    }
}

MODULE_STREAM_METHODS()
MODULE_LUA_METHODS()
{
    MODULE_STREAM_METHODS_REF(),
    { "stream_info", method_stream_info }
};
MODULE_LUA_REGISTER(analyze)
