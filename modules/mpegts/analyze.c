/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#include <astra.h>
#include "mpegts.h"

#include <sys/time.h>

#define LOG_MSG(_msg) "[analyze %s] " _msg, mod->config.name
#define UPDATING_INTERVAL 1000

typedef struct
{
    mpegts_packet_type_t type;

    uint8_t cc;

    uint32_t scrambled;
    uint32_t cc_error;
    uint32_t pes_error;

    uint32_t total_cc_error;
    uint32_t total_pes_error;

    uint32_t pcount;
    uint32_t bitrate;
} analyze_item_t;

struct module_data_s
{
    MODULE_BASE();
    MODULE_EVENT_BASE();

    struct
    {
        const char *name;
    } config;

    int stream_reload;
    mpegts_stream_t stream;
    analyze_item_t items[MAX_PID];

    uint32_t bitrate;
    uint32_t ts_error;

    int is_ready;
    int is_low_bitrate;
    int is_ts_error;
    int is_cc_error;
    int is_pes_error;
    int is_scrambled;

    void *rate_timer;
};

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
            return;
        default:
            log_error(LOG_MSG("PAT parse error %d"), psi->status);
            return;
    }

    mpegts_pat_dump(psi, mod->config.name);

    mpegts_pat_t *pat = psi->data;
    list_t *i = list_get_first(pat->items);
    while(i)
    {
        mpegts_pat_item_t *item = list_get_data(i);
        if(item->pnr > 0)
        {
            mod->items[item->pid].type = MPEGTS_PACKET_PMT;
            mod->stream[item->pid] = mpegts_pmt_init(item->pid);
        }
        else
        {
            mod->items[item->pid].type = MPEGTS_PACKET_NIT;
        }
        i = list_get_next(i);
    }
} /* scan_pat */

static void __check_desc(module_data_t *mod, mpegts_desc_t *desc, int is_pmt)
{
    if(!desc)
        return;

    const mpegts_packet_type_t type = (is_pmt)
                                    ? MPEGTS_PACKET_ECM
                                    : MPEGTS_PACKET_EMM;

    list_t *i = list_get_first(desc->items);
    while(i)
    {
        uint8_t *b = list_get_data(i);
        if(b[0] == 0x09)
        {
            const uint16_t cas_pid = ((b[4] & 0x1F) << 8) | b[5];
            mod->items[cas_pid].type = type;
        }
        i = list_get_next(i);
    }
}

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
            return;
        default:
            log_error(LOG_MSG("CAT parse error %d"), psi->status);
            return;
    }

    mpegts_cat_dump(psi, mod->config.name);

    mpegts_cat_t *cat = psi->data;
    __check_desc(mod, cat->desc, 0);
}

static void scan_pmt(module_data_t *mod, mpegts_psi_t *psi)
{
    if(mod->stream_reload)
        return;
    mpegts_pmt_parse(psi);
    switch(psi->status)
    {
        case MPEGTS_UNCHANGED:
            return;
        case MPEGTS_ERROR_NONE:
            break;
        case MPEGTS_CRC32_CHANGED:
            log_info(LOG_MSG("PMT changed, reload stream info"));
            mod->stream_reload = 1;
            return;
        default:
            log_error(LOG_MSG("PMT parse error %d"), psi->status);
            return;
    }

    mpegts_pmt_dump(psi, mod->config.name);

    mpegts_pmt_t *pmt = psi->data;

    __check_desc(mod, pmt->desc, 1);

    list_t *i = list_get_first(pmt->items);
    while(i)
    {
        mpegts_pmt_item_t *item = list_get_data(i);

        analyze_item_t *aitem = &mod->items[item->pid];
        aitem->type = mpegts_pes_type(item->type);

        __check_desc(mod, item->desc, 1);

        i = list_get_next(i);
    }

    mod->is_ready = 1;
    mod->is_low_bitrate = 0;
} /* scan_pmt */

#if DEBUG
static void scan_sdt(module_data_t *mod, mpegts_psi_t *psi)
{
    if(mod->stream_reload)
        return;
    mpegts_sdt_parse(psi);

    switch(psi->status)
    {
        case MPEGTS_UNCHANGED:
            return;
        case MPEGTS_ERROR_NONE:
            break;
        case MPEGTS_CRC32_CHANGED:
            log_info(LOG_MSG("SDT changed, reload stream info"));
            mod->stream_reload = 1;
            return;
        default:
            log_error(LOG_MSG("SDT parse error %d"), psi->status);
            return;
    }

    mpegts_sdt_dump(psi, mod->config.name);
} /* scan_sdt */
#endif

void rate_timer_callback(void *arg)
{
    module_data_t *mod = arg;

    int do_event = 0;

    int ts_error = 0;
    int cc_error = 0;
    int scrambled = 0;
    int pes_error = 0;

    // analyze ts headers

    const int last_bitrate = mod->bitrate / 10;
    mod->bitrate = 0;

    if(mod->ts_error)
    {
        mod->bitrate = mod->ts_error * TS_PACKET_SIZE * 8;
        mod->ts_error = 0;
        ts_error = 1;
    }

    /* all constants it's magic */
    for(int i = 0; i < MAX_PID; ++i)
    {
        analyze_item_t *item = &mod->items[i];
        const uint32_t pcount = item->pcount;
        item->pcount = 0;

        // analyze bitrate
        if(!pcount)
        {
            item->bitrate = 0;
            continue;
        }
        item->bitrate = pcount * TS_PACKET_SIZE * 8;
        mod->bitrate += item->bitrate;

        if(i == NULL_TS_PID)
            break;

        // analyze cc errors
        if(item->cc_error)
        {
            if(item->cc_error > (pcount / 5))
                cc_error = 1;
            item->total_cc_error += item->cc_error;
            item->cc_error = 0;
        }

        // analyze scrambled status
        if(item->scrambled)
        {
            if(item->scrambled == pcount)
                scrambled = 1;
            item->scrambled = 0;
        }

        // analyze pes-header errors
        if(item->pes_error)
        {
            if(item->pes_error > 2)
                pes_error = 1;
            item->total_pes_error += item->pes_error;
            item->pes_error = 0;
        }
    }

    if(!mod->bitrate || mod->bitrate < last_bitrate)
    {
        if(!mod->is_low_bitrate)
        {
            log_info(LOG_MSG("low bitrate: %uKbit/s"), mod->bitrate);
            mod->is_ready = 0;
            mod->stream_reload = 1;
            mod->is_low_bitrate = 1;
            module_event_call(mod);
        }
        return;
    }

    if(ts_error != mod->is_ts_error)
    {
        do_event = 1;
        log_info(LOG_MSG("TS header: %s"), (ts_error) ? "ERROR" : "OK");
        mod->is_ts_error = ts_error;
    }

    if(cc_error != mod->is_cc_error)
    {
        do_event = 1;
        log_info(LOG_MSG("CC: %s"), (cc_error) ? "ERROR" : "OK");
        mod->is_cc_error = cc_error;
    }

    if(scrambled != mod->is_scrambled)
    {
        do_event = 1;
        log_info(LOG_MSG("Scrambled: %s"), (scrambled) ? "YES" : "NO");
        mod->is_scrambled = scrambled;
    }

    if(pes_error != mod->is_pes_error)
    {
        do_event = 1;
        log_info(LOG_MSG("PES header: %s"), (pes_error) ? "ERROR" : "OK");
        mod->is_pes_error = pes_error;
    }

    if(do_event)
        module_event_call(mod);
}

/* stream_ts callbacks */

static void callback_send_ts(module_data_t *mod, uint8_t *ts)
{
    if(ts[0] != 0x47)
    {
        ++mod->ts_error;
        return;
    }

    const uint16_t pid = TS_PID(ts);
    analyze_item_t *item = &mod->items[pid];
    ++item->pcount;

    if(pid == NULL_TS_PID)
        return;

    if(mod->stream_reload)
    {
        mpegts_stream_destroy(mod->stream);
        mod->stream[0] = mpegts_pat_init();
        mod->stream_reload = 0;
        mod->is_ready = 0;
        module_event_call(mod);
    }

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
                break;
            case MPEGTS_PACKET_PMT:
                mpegts_psi_mux(psi, ts, scan_pmt, mod);
                break;
            case MPEGTS_PACKET_SDT:
#if DEBUG
                mpegts_psi_mux(psi, ts, scan_sdt, mod);
#endif
                break;
            default:
                break;
        }
    }

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
    {
        ++item->scrambled;
        return;
    }

    if(!(item->type & MPEGTS_PACKET_PES))
        return;

    if(TS_PUSI(ts))
    {
        uint8_t *payload = ts + 4;
        if(af == 0x30)
            payload += (ts[4] + 1);

        if(PES_HEADER(payload) != 0x000001)
            ++item->pes_error;
    }
}

/* methods */

static int method_status(module_data_t *mod)
{
    static char __bitrate[] = "bitrate";
    static char __scrambled[] = "scrambled";
    static char __pes_error[] = "pes_error";
    static char __cc_error[] = "cc_error";

    lua_State *L = LUA_STATE(mod);
    lua_newtable(L);

    lua_pushboolean(L, mod->is_ready);
    lua_setfield(L, -2, "ready");
    lua_pushnumber(L, mod->bitrate / 1000);
    lua_setfield(L, -2, __bitrate);

    uint32_t total_cc_error = 0;
    uint32_t total_pes_error = 0;

    lua_newtable(L);
    for(int i = 0; i < MAX_PID; ++i)
    {
        analyze_item_t *item = &mod->items[i];
        if(!(item->type || item->bitrate))
            continue;

        lua_pushnumber(L, i);
        lua_newtable(L);

        lua_pushnumber(L, item->type);
        lua_setfield(L, -2, "type");

        lua_pushnumber(L, item->bitrate);
        lua_setfield(L, -2, __bitrate);

        lua_pushnumber(L, item->total_cc_error);
        lua_setfield(L, -2, __cc_error);
        total_cc_error += item->total_cc_error;

        lua_pushnumber(L, item->total_pes_error);
        lua_setfield(L, -2, __pes_error);
        total_pes_error += item->total_pes_error;

        lua_pushboolean(L, item->scrambled);
        lua_setfield(L, -2, __scrambled);

        lua_settable(L, -3);
    }
    lua_setfield(L, -2, "stream");

    lua_pushnumber(L, total_cc_error);
    lua_setfield(L, -2, __cc_error);
    lua_pushnumber(L, total_pes_error);
    lua_setfield(L, -2, __pes_error);
    lua_pushboolean(L, mod->is_scrambled);
    lua_setfield(L, -2, __scrambled);

    return 1;
}

static int method_event(module_data_t *mod)
{
    module_event_set(mod);
    return 0;
}

/* required */

static void module_initialize(module_data_t *mod)
{
    module_set_string(mod, "name", 1, NULL, &mod->config.name);

    stream_ts_init(mod, callback_send_ts, NULL, NULL, NULL, NULL);

    mod->rate_timer = timer_attach(UPDATING_INTERVAL, rate_timer_callback, mod);

    mod->is_ts_error = -1;
    mod->is_scrambled = -1;

    mod->stream[0] = mpegts_pat_init();
    mod->items[0].type = MPEGTS_PACKET_PAT;
    mod->stream[1] = mpegts_cat_init();
    mod->items[1].type = MPEGTS_PACKET_CAT;
    mod->stream[17] = mpegts_sdt_init();
    mod->items[17].type = MPEGTS_PACKET_SDT;
}

static void module_destroy(module_data_t *mod)
{
    stream_ts_destroy(mod);
    module_event_destroy(mod);

    timer_detach(mod->rate_timer);

    mpegts_stream_destroy(mod->stream);
}

MODULE_METHODS()
{
    METHOD(status)
    METHOD(event)
};

MODULE(analyze)
