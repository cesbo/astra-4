/*
 * Astra Module Stream API
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#ifndef _MODULE_STREAM_H_
#define _MODULE_STREAM_H_ 1

#include "base.h"
#include "module_lua.h"
#include <core/asc.h>
#include <sys/queue.h>

#ifndef TAILQ_FOREACH_SAFE
#define TAILQ_FOREACH_SAFE(var, head, field, tvar)                      \
        for ((var) = TAILQ_FIRST((head));                               \
            (var) && ((tvar) = TAILQ_NEXT((var), field), 1);            \
            (var) = (tvar))
#endif

typedef struct module_stream_t module_stream_t;
struct module_stream_t
{
    module_data_t *self;
    module_stream_t *parent;

    // stream
    void (*on_ts)(module_data_t *mod, const uint8_t *ts);

    TAILQ_ENTRY(module_stream_t) entries;
    TAILQ_HEAD(a_list_t, module_stream_t) childs;

    // demux
    void (*join_pid)(module_data_t *mod, uint16_t pid);
    void (*leave_pid)(module_data_t *mod, uint16_t pid);

    uint8_t *pid_list;
};

#define MODULE_STREAM_DATA() module_stream_t __stream

// stream

void __module_stream_init(module_stream_t *stream);
void __module_stream_destroy(module_stream_t *stream);
void __module_stream_attach(module_stream_t *stream, module_stream_t *child);
void __module_stream_send(module_stream_t *stream, const uint8_t *ts);

#define module_stream_init(_mod, _on_ts)                                                        \
    {                                                                                           \
        _mod->__stream.self = _mod;                                                             \
        _mod->__stream.on_ts = _on_ts;                                                          \
        __module_stream_init(&mod->__stream);                                                   \
        lua_getfield(lua, MODULE_OPTIONS_IDX, "upstream");                                      \
        if(lua_type(lua, -1) == LUA_TLIGHTUSERDATA)                                             \
            __module_stream_attach(lua_touserdata(lua, -1), &mod->__stream);                    \
        lua_pop(lua, 1);                                                                        \
    }

#define module_stream_demux_set(_mod, _join_pid, _leave_pid)                                    \
    {                                                                                           \
        _mod->__stream.pid_list = calloc(MAX_PID, sizeof(uint8_t));                             \
        _mod->__stream.join_pid = _join_pid;                                                    \
        _mod->__stream.leave_pid = _leave_pid;                                                  \
    }

#define module_stream_destroy(_mod)                                                             \
    {                                                                                           \
        if(_mod->__stream.pid_list)                                                             \
        {                                                                                       \
            for(int __i = 0; __i < MAX_PID; ++__i)                                              \
            {                                                                                   \
                if(_mod->__stream.pid_list[__i])                                                \
                {                                                                               \
                    module_stream_demux_leave_pid(_mod, __i);                                   \
                }                                                                               \
            }                                                                                   \
            free(_mod->__stream.pid_list);                                                      \
        }                                                                                       \
        __module_stream_destroy(&_mod->__stream);                                               \
        _mod->__stream.self = NULL;                                                             \
    }

#define module_stream_send(_mod, _ts)                                                           \
    __module_stream_send(&_mod->__stream, _ts)

// demux

#define module_stream_demux_check_pid(_mod, _pid)                                               \
    (_mod->__stream.pid_list && _mod->__stream.pid_list[_pid] > 0)

#define module_stream_demux_join_pid(_mod, _pid)                                                \
    {                                                                                           \
        const uint16_t __pid = _pid;                                                            \
        if(_mod->__stream.pid_list && !_mod->__stream.pid_list[__pid])                          \
        {                                                                                       \
            _mod->__stream.pid_list[__pid] = 1;                                                 \
            if(_mod->__stream.parent && _mod->__stream.parent->join_pid)                        \
                _mod->__stream.parent->join_pid(_mod->__stream.parent->self, __pid);            \
        }                                                                                       \
    }

#define module_stream_demux_leave_pid(_mod, _pid)                                               \
    {                                                                                           \
        const uint16_t __pid = _pid;                                                            \
        if(_mod->__stream.pid_list && _mod->__stream.pid_list[__pid])                           \
        {                                                                                       \
            _mod->__stream.pid_list[__pid] = 0;                                                 \
            if(_mod->__stream.parent && _mod->__stream.parent->leave_pid)                       \
                _mod->__stream.parent->leave_pid(_mod->__stream.parent->self, __pid);           \
        }                                                                                       \
    }

// base

#define MODULE_STREAM_METHODS()                                                                 \
    static int module_stream_stream(module_data_t *mod)                                         \
    {                                                                                           \
        lua_pushlightuserdata(lua, &mod->__stream);                                             \
        return 1;                                                                               \
    }

#define MODULE_STREAM_METHODS_REF()                                                             \
    { "stream", module_stream_stream }

#endif /* _MODULE_STREAM_H_ */
