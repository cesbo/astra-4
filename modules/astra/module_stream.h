/*
 * Astra Module: Stream API
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

#ifndef _MODULE_STREAM_H_
#define _MODULE_STREAM_H_ 1

#include "base.h"
#include "module_lua.h"
#include <core/asc.h>

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
    (_mod->__stream.pid_list[_pid] > 0)

#define module_stream_demux_join_pid(_mod, _pid)                                                \
    {                                                                                           \
        const uint16_t __pid = _pid;                                                            \
        asc_assert(_mod->__stream.pid_list != NULL                                              \
                   , "%s:%d module_stream_demux_set() is required", __FILE__, __LINE__);        \
        ++_mod->__stream.pid_list[__pid];                                                       \
        if(_mod->__stream.pid_list[__pid] == 1                                                  \
           && _mod->__stream.parent                                                             \
           && _mod->__stream.parent->join_pid)                                                  \
        {                                                                                       \
            _mod->__stream.parent->join_pid(_mod->__stream.parent->self, __pid);                \
        }                                                                                       \
    }

#define module_stream_demux_leave_pid(_mod, _pid)                                               \
    {                                                                                           \
        const uint16_t __pid = _pid;                                                            \
        asc_assert(_mod->__stream.pid_list != NULL                                              \
                   , "%s:%d module_stream_demux_set() is required", __FILE__, __LINE__);        \
        if(_mod->__stream.pid_list[__pid] > 0)                                                  \
        {                                                                                       \
            --_mod->__stream.pid_list[__pid];                                                   \
            if(_mod->__stream.pid_list[__pid] == 0                                              \
               && _mod->__stream.parent                                                         \
               && _mod->__stream.parent->leave_pid)                                             \
            {                                                                                   \
                _mod->__stream.parent->leave_pid(_mod->__stream.parent->self, __pid);           \
            }                                                                                   \
        }                                                                                       \
        else                                                                                    \
        {                                                                                       \
            asc_log_error("%s:%d module_stream_demux_leave_pid() double call pid:%d"            \
                          , __FILE__, __LINE__, __pid);                                         \
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
