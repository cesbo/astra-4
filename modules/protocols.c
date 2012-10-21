/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#include <astra.h>
#include <modules/mpegts/mpegts.h>

#define LOG_MSG(_msg) "[core/module] " _msg

struct module_data_s
{
    MODULE_BASE();
};

#ifdef ASTRA_STREAM_TS_API

struct protocol_stream_ts_s
{
    stream_ts_send_ts_t send_ts;
    stream_ts_on_attach_t on_attach;
    stream_ts_on_detach_t on_detach;
    stream_ts_join_pid_t join_pid;
    stream_ts_leave_pid_t leave_pid;
    module_data_t *parent;
    list_t *child;
    unsigned char joined[MAX_PID];
};

void stream_ts_init(module_data_t *mod, stream_ts_send_ts_t send_ts
                    , stream_ts_on_attach_t on_attach
                    , stream_ts_on_detach_t on_detach
                    , stream_ts_join_pid_t join_pid
                    , stream_ts_leave_pid_t leave_pid)
{
    if(mod->__protocols.stream_ts)
        return;

    protocol_stream_ts_t *s = calloc(1, sizeof(protocol_stream_ts_t));
    s->send_ts = send_ts;
    s->on_attach = on_attach;
    s->on_detach = on_detach;
    s->join_pid = join_pid;
    s->leave_pid = leave_pid;
    mod->__protocols.stream_ts = s;
}

static void _stream_ts_protocol_error(module_data_t *mod)
{
    log_error("[core/module] module %s isn't support protocol stream_ts"
              , mod->__name);
}

static void __stream_ts_detach(module_data_t *mod, module_data_t *child)
{
    protocol_stream_ts_t *s = mod->__protocols.stream_ts;
    protocol_stream_ts_t *cs = child->__protocols.stream_ts;
    if(!s)
    {
        _stream_ts_protocol_error(mod);
        return;
    }
    if(!cs)
    {
        _stream_ts_protocol_error(child);
        return;
    }
    if(cs->on_detach)
        cs->on_detach(child, mod);
    s->child = list_get_first(list_delete(s->child, child));
    cs->parent = NULL;
}

void stream_ts_destroy(module_data_t *mod)
{
    if(!mod->__protocols.stream_ts)
        return;

    // detach module
    protocol_stream_ts_t *s = mod->__protocols.stream_ts;
    if(s->parent)
        __stream_ts_detach(s->parent, mod);
    list_t *i = list_get_first(s->child);
    while(i)
    {
        module_data_t *child = list_get_data(i);
        child->__protocols.stream_ts->parent = NULL;
        i = list_delete(i, NULL);
    }

    free(s);
    mod->__protocols.stream_ts = NULL;
}

inline void stream_ts_send(module_data_t *mod, unsigned char *ts)
{
    list_t *i = list_get_first(mod->__protocols.stream_ts->child);
    while(i)
    {
        module_data_t *child = list_get_data(i);
        child->__protocols.stream_ts->send_ts(child, ts);
        i = list_get_next(i);
    }
}

inline void stream_ts_sendto(module_data_t *dst, unsigned char *ts)
{
    dst->__protocols.stream_ts->send_ts(dst, ts);
}

void stream_ts_attach(module_data_t *mod)
{
    lua_State *L = LUA_STATE(mod);
    luaL_checktype(L, 2, LUA_TUSERDATA);
    module_data_t *child = lua_touserdata(L, 2);

    protocol_stream_ts_t *s = mod->__protocols.stream_ts;
    protocol_stream_ts_t *cs = child->__protocols.stream_ts;
    if(!s)
    {
        _stream_ts_protocol_error(mod);
        return;
    }
    if(!cs)
    {
        _stream_ts_protocol_error(child);
        return;
    }
    if(cs->parent)
    {
        log_error("[protocol stream_ts] attach %s to %s failed "
                  "(already attached)"
                  , child->__name, mod->__name);
        return;
    }
    cs->parent = mod;
    s->child = list_insert(s->child, child);
    if(cs->on_attach)
        cs->on_attach(child, mod);
}

void stream_ts_detach(module_data_t *mod)
{
    lua_State *L = LUA_STATE(mod);
    luaL_checktype(L, 2, LUA_TUSERDATA);
    module_data_t *child = lua_touserdata(L, 2);

    __stream_ts_detach(mod, child);
}

void stream_ts_join_pid(module_data_t *mod, unsigned short pid)
{
    protocol_stream_ts_t *s = mod->__protocols.stream_ts;
    if(s->joined[pid])
        return;
    protocol_stream_ts_t *ps = s->parent->__protocols.stream_ts;
    if(ps && ps->join_pid)
    {
        ps->join_pid(s->parent, mod, pid);
        s->joined[pid] = 1;
    }
}

void stream_ts_leave_pid(module_data_t *mod, unsigned short pid)
{
    protocol_stream_ts_t *s = mod->__protocols.stream_ts;
    if(!s->joined[pid])
        return;
    protocol_stream_ts_t *ps = s->parent->__protocols.stream_ts;
    if(ps && ps->leave_pid)
    {
        ps->leave_pid(s->parent, mod, pid);
        s->joined[pid] = 0;
    }
}

void stream_ts_leave_all(module_data_t *mod)
{
    protocol_stream_ts_t *s = mod->__protocols.stream_ts;
    protocol_stream_ts_t *ps = s->parent->__protocols.stream_ts;
    if(!(ps && ps->leave_pid))
        return;
    for(int i = 0; i < MAX_PID; ++i)
    {
        if(!s->joined[i])
            continue;
        ps->leave_pid(s->parent, mod, i);
        s->joined[i] = 0;
    }
}

#endif /* ASTRA_STREAM_TS_API */

