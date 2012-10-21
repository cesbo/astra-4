/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#include <astra.h>
#include "mpegts.h"

#define LOG_MSG(_msg) "[channel %s] " _msg, mod->config.name
#define PAT_PMT_INTERVAL 500

struct module_data_s
{
    MODULE_BASE();

    struct
    {
        char *name;
        int pid; // temporary
        int pnr;
        int sdt;
    } config;

    int stream_reload;
    mpegts_stream_t stream;

    // on init
    list_t *pid_attach;
    uint16_t pid_map[MAX_PID]; // < MAX_PID - map; == 0 - drop
    int filter;
    list_t *pid_order;

    // custom PAT/PMT
    uint8_t pat_version;
    mpegts_psi_t *pat;
    mpegts_psi_t *pmt;
    void *pat_pmt_timer;
};

/* module code */

static void send_custom_pat_pmt(void *arg)
{
    module_data_t *mod = arg;
    if(mod->pat)
        mpegts_psi_demux(mod->pat, stream_ts_send, mod);
    if(mod->pmt)
        mpegts_psi_demux(mod->pmt, stream_ts_send, mod);
}

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

    mod->pat = mpegts_pat_init();
    ((mpegts_pat_t *)mod->pat->data)->version = mod->pat_version;
    mod->pat_version = (mod->pat_version + 1) & 0x1F;

    list_t *i = list_get_first(pat->items);
    while(i)
    {
        mpegts_pat_item_t *item = list_get_data(i);
        if(mod->config.pnr == item->pnr)
        {
            const uint16_t pmt_pid = item->pid;
            mod->stream[pmt_pid] = mpegts_pmt_init(pmt_pid);
            mpegts_pmt_t *pmt = mod->stream[pmt_pid]->data;
            pmt->pnr = item->pnr;
            stream_ts_join_pid(mod, pmt_pid);
        }
        i = list_get_next(i);
    }
} /* scan_pat */

static void __npmt_set_item(module_data_t *mod
                            , mpegts_pmt_t *opmt
                            , mpegts_pmt_item_t *item)
{
    mpegts_pmt_t *pmt = mod->pmt->data;

    const uint16_t pid = item->pid;
    if(!mod->pid_map[pid])
    {
        if(mod->filter)
            return;
        mod->pid_map[pid] = pid;
    }
    const uint16_t custom_pid = mod->pid_map[pid];

    mpegts_pmt_item_add(mod->pmt, custom_pid, item->type, item->desc);
    stream_ts_join_pid(mod, pid);

    if(opmt->pcr == pid)
        pmt->pcr = custom_pid;
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
        default:
            return;
    }

    mpegts_pmt_t *opmt = psi->data;

    // duplicate pmt
    mod->pmt = mpegts_pmt_init(psi->pid);
    mpegts_pmt_t *pmt = mod->pmt->data;
    pmt->pnr = opmt->pnr;
    if(opmt->desc)
    {
        pmt->desc = mpegts_desc_init(opmt->desc->buffer
                                     , opmt->desc->buffer_size);
    }
    pmt->version = opmt->version;
    pmt->current_next = opmt->current_next;

    mpegts_pat_item_add(mod->pat, psi->pid, pmt->pnr);
    mpegts_pat_assemble(mod->pat);

    if(mod->pid_order)
    {
        list_t *i = list_get_first(mod->pid_order);
        for(; i; i = list_get_next(i))
        {
            uint16_t pid = (uint16_t)(intptr_t)list_get_data(i);
            mpegts_pmt_item_t *item = mpegts_pmt_item_get(psi, pid);
            if(!item)
            {
                log_warning(LOG_MSG("item with pid:%d is not found"), pid);
                continue;
            }
            __npmt_set_item(mod, opmt, item);
        }
    }
    else
    {
        list_t *i = list_get_first(opmt->items);
        for(; i; i = list_get_next(i))
        {
            mpegts_pmt_item_t *item = list_get_data(i);
            __npmt_set_item(mod, opmt, item);
        }
    }

    if(!pmt->pcr)
    {
        if(!mod->pid_map[opmt->pcr])
            mod->pid_map[opmt->pcr] = opmt->pcr;
        pmt->pcr = mod->pid_map[opmt->pcr];
        stream_ts_join_pid(mod, opmt->pcr);
    }

    mpegts_pmt_assemble(mod->pmt);
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
        default:
            return;
    }

    // DEBUG:
    mpegts_sdt_dump(psi, mod->config.name);
} /* scan_sdt */
#endif

/* stream_ts callbacks */

static void callback_on_attach(module_data_t *, module_data_t *);

static void callback_send_ts(module_data_t *mod, uint8_t *ts)
{
    if(mod->stream_reload)
    {
        const int join_pat = (mod->stream[0] != NULL);
        mpegts_stream_destroy(mod->stream);
        stream_ts_leave_all(mod);

        if(join_pat)
        {
            mod->stream[0] = mpegts_pat_init();

            mpegts_pat_destroy(mod->pat);
            mpegts_pmt_destroy(mod->pmt);
            mod->pat = NULL;
            mod->pmt = NULL;

            callback_on_attach(mod, NULL);
        }
        mod->stream_reload = 0;
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
        return;
    }

    const uint16_t custom_pid = mod->pid_map[pid];
    if(custom_pid == 0)
        return;
    else if(custom_pid != pid)
    {
        uint8_t map_ts[TS_PACKET_SIZE];
        memcpy(map_ts, ts, TS_PACKET_SIZE);
        map_ts[1] = (ts[1] & 0xE0) | (custom_pid >> 8);
        map_ts[2] = custom_pid & 0xFF;
        stream_ts_send(mod, map_ts);
        return;
    }
    else
        stream_ts_send(mod, ts);
}

static void callback_on_attach(module_data_t *mod, module_data_t *parent)
{
    if(mod->stream[0])
        stream_ts_join_pid(mod, 0); // attach PAT

    if(mod->stream[17])
        stream_ts_join_pid(mod, 17); // attach SDT

    list_t *i = list_get_first(mod->pid_attach);
    while(i)
    {
        uint16_t pid = (uint16_t)(intptr_t)list_get_data(i);
        stream_ts_join_pid(mod, pid);
        i = list_get_next(i);
    }
}

static void callback_on_detach(module_data_t *mod, module_data_t *parent)
{
    stream_ts_leave_all(mod);
}

static void callback_join_pid(module_data_t *mod
                              , module_data_t *child
                              , uint16_t pid)
{
    mod->pid_map[pid] = pid;
    stream_ts_join_pid(mod, pid);
}

static void callback_leave_pid(module_data_t *mod
                               , module_data_t *child
                               , uint16_t pid)
{
    mod->pid_map[pid] = 0;
    stream_ts_leave_pid(mod, pid);
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

/* required */

static void module_init(module_data_t *mod)
{
    log_debug(LOG_MSG("init"));

    stream_ts_init(mod, callback_send_ts
                   , callback_on_attach, callback_on_detach
                   , callback_join_pid, callback_leave_pid);

    if(mod->config.pnr)
    {
        mod->stream[0] = mpegts_pat_init();
        mod->pat_pmt_timer = timer_attach(PAT_PMT_INTERVAL
                                          , send_custom_pat_pmt, mod);
    }

    if(mod->config.sdt)
        mod->stream[17] = mpegts_sdt_init();
}

static void module_destroy(module_data_t *mod)
{
    log_debug(LOG_MSG("destroy"));

    stream_ts_destroy(mod);

    if(mod->pat_pmt_timer)
        timer_detach(mod->pat_pmt_timer);

    while(mod->pid_order)
        mod->pid_order = list_delete(mod->pid_order, NULL);

    // custom tables
    if(mod->pat)
        mpegts_pat_destroy(mod->pat);
    if(mod->pmt)
        mpegts_pmt_destroy(mod->pmt);

    mpegts_stream_destroy(mod->stream);
}

/* config_check */

static int config_check_pid(module_data_t *mod)
{
    if(mod->config.pid >= MAX_PID)
    {
        log_error(LOG_MSG("pid value must be less then %d"), MAX_PID);
        return 0;
    }

    log_warning(LOG_MSG("option pid is not yet ready"));

    return 1;
}

static int config_check_filter(module_data_t *mod)
{
    lua_State *L = LUA_STATE(mod);
    const int val = lua_gettop(L);
    luaL_checktype(L, val, LUA_TTABLE);

    for(lua_pushnil(L); lua_next(L, val); lua_pop(L, 1))
    {
        const int pid = lua_tonumber(L, -1);
        if(pid < MAX_PID && !mod->pid_map[pid])
        {
            mod->pid_map[pid] = pid;
            mod->pid_order = list_append(mod->pid_order, (void *)(intptr_t)pid);
        }
    }

    mod->filter = 1;

    return 1;
}

static int config_check_map(module_data_t *mod)
{
    lua_State *L = LUA_STATE(mod);
    const int val = lua_gettop(L);
    luaL_checktype(L, val, LUA_TTABLE);

    for(lua_pushnil(L); lua_next(L, val); )
    {
        const int pid = lua_tonumber(L, -1);
        lua_pop(L, 1);
        if(!lua_next(L, val))
            break;
        const int custom_pid = lua_tonumber(L, -1);
        lua_pop(L, 1);
        if(pid < MAX_PID && custom_pid < MAX_PID)
            mod->pid_map[pid] = custom_pid;
    }

    return 1;
}

MODULE_OPTIONS()
{
    OPTION_STRING("name", config.name, NULL)
    OPTION_NUMBER("pid", config.pid, config_check_pid)
    OPTION_NUMBER("pnr", config.pnr, NULL)
    OPTION_NUMBER("sdt", config.sdt, NULL)
    OPTION_CUSTOM("filter", config_check_filter)
    OPTION_CUSTOM("map", config_check_map)
};

MODULE_METHODS()
{
    METHOD(attach)
    METHOD(detach)
};

MODULE(channel)
