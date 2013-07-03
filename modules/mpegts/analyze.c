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
 *      rate_stat   - boolean, dump bitrate with 10ms interval
 *      callback    - function(data), events callback:
 *                    data.error    - string,
 *                    data.psi      - table, psi information (PAT, PMT, CAT, SDT)
 *                    data.analyze  - table, per pid information: errors, bitrate
 *                    data.on_air   - boolean, comes with data.analyze, stream status
 *                    data.rate     - table, rate_stat array
 */

#include <astra.h>

typedef struct
{
    mpegts_packet_type_t type;

    uint8_t cc;

    uint32_t packets;

    // errors
    uint32_t cc_error;  // Continuity Counter
    uint32_t sc_error;  // Scrambled
    uint32_t pes_error; // PES header
} analyze_item_t;

typedef struct
{
    uint16_t pnr;
    uint32_t crc;
} pmt_checksum_t;

struct module_data_t
{
    MODULE_LUA_DATA();
    MODULE_STREAM_DATA();

    const char *name;
    int rate_stat;

    uint16_t tsid;

    asc_timer_t *check_stat;
    analyze_item_t stream[MAX_PID];

    mpegts_psi_t *pat;
    mpegts_psi_t *cat;
    mpegts_psi_t *pmt;
    mpegts_psi_t *sdt;

    int pmt_count;
    pmt_checksum_t *pmt_checksum_list;

    // rate_stat
    struct timeval last_ts;
    uint32_t ts_count;
    int rate_count;
    int rate[10];

    // on_air
    uint32_t last_bitrate;
};

#define MSG(_msg) "[analyze %s] " _msg, mod->name

static const char __type[] = "type";
static const char __pid[] = "pid";
static const char __crc32[] = "crc32";
static const char __pnr[] = "pnr";
static const char __tsid[] = "tsid";
static const char __descriptors[] = "descriptors";
static const char __psi[] = "psi";
static const char __err[] = "error";
static const char __callback[] = "callback";

static void do_callback(module_data_t *mod)
{
    asc_assert((lua_type(lua, -1) == LUA_TTABLE), "table required");

    lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->__lua.oref);
    lua_getfield(lua, -1, __callback);
    lua_remove(lua, -2);

    lua_pushvalue(lua, -2);
    lua_call(lua, 1, 0);
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

    lua_pushnumber(lua, psi->pid);
    lua_setfield(lua, -2, __pid);

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
    {
        lua_pushstring(lua, "PAT checksum error");
        lua_setfield(lua, -2, __err);
        do_callback(mod);
        lua_pop(lua, 1); // info
        return;
    }

    psi->crc32 = crc32;
    mod->tsid = PAT_GET_TSID(psi);

    lua_pushstring(lua, "pat");
    lua_setfield(lua, -2, __psi);

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

        mod->stream[pid].type = (pnr) ? MPEGTS_PACKET_PMT : MPEGTS_PACKET_NIT;

        PAT_ITEMS_NEXT(psi, pointer);
    }
    lua_setfield(lua, -2, "programs");

    mod->pmt_count = programs_count;
    if(mod->pmt_checksum_list)
        free(mod->pmt_checksum_list);
    mod->pmt_checksum_list = calloc(mod->pmt_count, sizeof(pmt_checksum_t));

    do_callback(mod);
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

    lua_pushnumber(lua, psi->pid);
    lua_setfield(lua, -2, __pid);

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
    {
        lua_pushstring(lua, "CAT checksum error");
        lua_setfield(lua, -2, __err);
        do_callback(mod);
        lua_pop(lua, 1); // info
        return;
    }
    psi->crc32 = crc32;

    lua_pushstring(lua, "cat");
    lua_setfield(lua, -2, __psi);

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

    do_callback(mod);
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

    const uint32_t crc32 = PSI_GET_CRC32(psi);

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
    {
        lua_newtable(lua);

        lua_pushnumber(lua, psi->pid);
        lua_setfield(lua, -2, __pid);

        lua_pushstring(lua, "PMT checksum error");
        lua_setfield(lua, -2, __err);
        do_callback(mod);
        lua_pop(lua, 1); // info
        return;
    }

    const uint16_t pnr = PMT_GET_PNR(psi);

    // check changes
    for(int i = 0; i < mod->pmt_count; ++i)
    {
        if(mod->pmt_checksum_list[i].pnr == pnr || mod->pmt_checksum_list[i].pnr == 0)
        {
            if(mod->pmt_checksum_list[i].crc == crc32)
                return;

            mod->pmt_checksum_list[i].pnr = pnr;
            mod->pmt_checksum_list[i].crc = crc32;
            break;
        }
    }

    lua_newtable(lua);

    lua_pushnumber(lua, psi->pid);
    lua_setfield(lua, -2, __pid);

    lua_pushstring(lua, "pmt");
    lua_setfield(lua, -2, __psi);

    lua_pushnumber(lua, crc32);
    lua_setfield(lua, -2, __crc32);

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

        mod->stream[pid].type = mpegts_pes_type(type);
        lua_pushstring(lua, mpegts_type_name(mod->stream[pid].type));
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

    do_callback(mod);
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

    lua_pushnumber(lua, psi->pid);
    lua_setfield(lua, -2, __pid);

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
    {
        lua_pushstring(lua, "SDT checksum error");
        lua_setfield(lua, -2, __err);
        do_callback(mod);
        lua_pop(lua, 1); // info
        return;
    }
    psi->crc32 = crc32;

    lua_pushstring(lua, "sdt");
    lua_setfield(lua, -2, __psi);

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
            mpegts_desc_to_lua(desc_pointer);
            lua_settable(lua, -3);

            SDT_ITEM_DESC_NEXT(pointer, desc_pointer);
        }
        lua_setfield(lua, -2, __descriptors);

        lua_settable(lua, -3); // append to the "services[X].descriptors" table

        SDT_ITEMS_NEXT(psi, pointer);
    }
    lua_setfield(lua, -2, "services");

    do_callback(mod);
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

static void append_rate(module_data_t *mod, int rate)
{
    mod->rate[mod->rate_count] = rate;
    ++mod->rate_count;
    if(mod->rate_count >= (int)(sizeof(mod->rate)/sizeof(*mod->rate)))
    {
        lua_newtable(lua);
        lua_newtable(lua);
        for(int i = 0; i < mod->rate_count; ++i)
        {
            lua_pushnumber(lua, i + 1);
            lua_pushnumber(lua, mod->rate[i]);
            lua_settable(lua, -3);
        }
        lua_setfield(lua, -2, "rate");
        do_callback(mod);
        mod->rate_count = 0;
    }
}

static void on_ts(module_data_t *mod, const uint8_t *ts)
{
    if(mod->rate_stat)
    {
        ++mod->ts_count;

        int diff_interval = 0;
        struct timeval tv;
        gettimeofday(&tv, NULL);
        tv.tv_usec /= 10000;
        const int64_t s = mod->last_ts.tv_sec * 100 + mod->last_ts.tv_usec;
        const int64_t e = tv.tv_sec * 100 + tv.tv_usec;
        if(e != s)
        {
            memcpy(&mod->last_ts, &tv, sizeof(struct timeval));
            if(s > 0)
                diff_interval = e - s;
        }

        if(diff_interval > 0)
        {
            if(diff_interval > 1)
            {
                for(; diff_interval > 0; --diff_interval)
                    append_rate(mod, 0);
            }

            append_rate(mod, mod->ts_count);
            mod->ts_count = 0;
        }
    }

    const uint16_t pid = TS_PID(ts);

    analyze_item_t *item = &mod->stream[pid];
    const mpegts_packet_type_t type = item->type;

    ++item->packets;

    if(type == MPEGTS_PACKET_NULL)
        return;

    if(type & (MPEGTS_PACKET_PSI | MPEGTS_PACKET_SI))
    {
        switch(type)
        {
            case MPEGTS_PACKET_PAT:
                mpegts_psi_mux(mod->pat, ts, on_pat, mod);
                break;
            case MPEGTS_PACKET_CAT:
                mpegts_psi_mux(mod->cat, ts, on_cat, mod);
                break;
            case MPEGTS_PACKET_PMT:
                mod->pmt->pid = pid;
                mpegts_psi_mux(mod->pmt, ts, on_pmt, mod);
                break;
            case MPEGTS_PACKET_SDT:
                mpegts_psi_mux(mod->sdt, ts, on_sdt, mod);
                break;
            default:
                break;
        }
    }

    // Analyze

    const uint8_t af = TS_AF(ts);
    // skip packets without payload
    if(!(af & 0x10))
        return;

    const uint8_t cc = TS_CC(ts);
    const uint8_t last_cc = (item->cc + 1) & 0x0F;
    item->cc = cc;

    if(cc != last_cc)
        ++item->cc_error;

    if(TS_SC(ts))
        ++item->sc_error;

    if(!(type & MPEGTS_PACKET_PES))
        return;

    if(TS_PUSI(ts))
    {
        const uint8_t *payload = ts + 4;
        if(af == 0x30)
            payload += (ts[4] + 1);

        if(PES_HEADER(payload) != 0x000001)
            ++item->pes_error;
    }
}

static void on_check_stat(void *arg)
{
    module_data_t *mod = arg;

    int items_count = 1;
    lua_newtable(lua);

    bool on_air = true;

    uint32_t bitrate = 0;

    lua_newtable(lua);
    for(int i = 0; i < MAX_PID; ++i)
    {
        analyze_item_t *item = &mod->stream[i];

        if(item->type == MPEGTS_PACKET_UNKNOWN)
            continue;

        lua_pushnumber(lua, items_count++);
        lua_newtable(lua);

        lua_pushnumber(lua, i);
        lua_setfield(lua, -2, __pid);

        const uint32_t item_bitrate = (item->packets * TS_PACKET_SIZE * 8) / 1000;
        bitrate += item_bitrate;

        lua_pushnumber(lua, item_bitrate);
        lua_setfield(lua, -2, "bitrate");

        lua_pushnumber(lua, item->cc_error);
        lua_setfield(lua, -2, "cc_error");
        lua_pushnumber(lua, item->sc_error);
        lua_setfield(lua, -2, "sc_error");
        lua_pushnumber(lua, item->pes_error);
        lua_setfield(lua, -2, "pes_error");

        if(item->type == MPEGTS_PACKET_VIDEO)
        {
            if(item->sc_error)
                on_air = false;
            if(item->pes_error > 2)
                on_air = false;
        }

        item->packets = 0;
        item->cc_error = 0;
        item->sc_error = 0;
        item->pes_error = 0;

        lua_settable(lua, -3);
    }
    lua_setfield(lua, -2, "analyze");

    const uint32_t half_last_bitrate = mod->last_bitrate / 2;
    if(bitrate <= half_last_bitrate)
        on_air = false;
    else
        mod->last_bitrate = bitrate;

    lua_pushboolean(lua, on_air);
    lua_setfield(lua, -2, "on_air");

    do_callback(mod);

    lua_pop(lua, 1); // table
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
    asc_assert(mod->name != NULL, "[analyze] option 'name' is required");

    lua_getfield(lua, 2, __callback);
    asc_assert(lua_type(lua, -1) == LUA_TFUNCTION, MSG("option 'callback' is required"));
    lua_pop(lua, 1);

    module_option_number("rate_stat", &mod->rate_stat);

    module_stream_init(mod, on_ts);

    // PAT
    mod->stream[0x00].type = MPEGTS_PACKET_PAT;
    mod->pat = mpegts_psi_init(MPEGTS_PACKET_PAT, 0x00);
    // CAT
    mod->stream[0x01].type = MPEGTS_PACKET_CAT;
    mod->cat = mpegts_psi_init(MPEGTS_PACKET_CAT, 0x01);
    // SDT
    mod->stream[0x11].type = MPEGTS_PACKET_SDT;
    mod->sdt = mpegts_psi_init(MPEGTS_PACKET_SDT, 0x11);
    // PMT
    mod->pmt = mpegts_psi_init(MPEGTS_PACKET_PMT, MAX_PID);
    // NULL
    mod->stream[NULL_TS_PID].type = MPEGTS_PACKET_NULL;

    mod->check_stat = asc_timer_init(1000, on_check_stat, mod);
}

static void module_destroy(module_data_t *mod)
{
    module_stream_destroy(mod);

    mpegts_psi_destroy(mod->pat);
    mpegts_psi_destroy(mod->cat);
    mpegts_psi_destroy(mod->sdt);
    mpegts_psi_destroy(mod->pmt);

    asc_timer_destroy(mod->check_stat);

    if(mod->pmt_checksum_list)
        free(mod->pmt_checksum_list);
}

MODULE_STREAM_METHODS()
MODULE_LUA_METHODS()
{
    MODULE_STREAM_METHODS_REF()
};
MODULE_LUA_REGISTER(analyze)
