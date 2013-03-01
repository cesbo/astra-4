/*
 * Astra Module Demux API
 * http://cesbo.com/astra
 *
 * Copyright (C) 2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#ifndef _MODULE_DEMUX_H_
#define _MODULE_DEMUX_H_ 1

#include "mpegts.h"

typedef struct module_demux_t module_demux_t;
struct module_demux_t
{
    void (*join_pid)(module_data_t *mod, uint16_t pid);
    void (*leave_pid)(module_data_t *mod, uint16_t pid);

    module_data_t *self;
    module_demux_t *parent;

    uint8_t pid_list[MAX_PID];
};

#define MODULE_DEMUX_DATA() module_demux_t __demux

#define demux_is_pid(_mod, _pid) (_mod->__demux.pid_list[_pid] > 0)

#define demux_join_pid(_mod, _pid)                                                              \
    {                                                                                           \
        ++_mod->__demux.pid_list[_pid];                                                         \
        if(   _mod->__demux.parent                                                              \
           && _mod->__demux.parent->join_pid                                                    \
           && (_mod->__demux.pid_list[_pid] == 1))                                              \
        {                                                                                       \
            _mod->__demux.parent->join_pid(_mod->__demux.parent->self, _pid);                   \
        }                                                                                       \
    }

#define demux_leave_pid(_mod, _pid)                                                             \
    {                                                                                           \
        if(_mod->__demux.pid_list[_pid] > 0)                                                    \
        {                                                                                       \
            --_mod->__demux.pid_list[_pid];                                                     \
            if(   _mod->__demux.parent                                                          \
               && _mod->__demux.parent->leave_pid                                               \
               && _mod->__demux.pid_list[_pid] == 0)                                            \
            {                                                                                   \
                _mod->__demux.parent->leave_pid(_mod->__demux.parent->self, _pid);              \
            }                                                                                   \
        }                                                                                       \
    }

#define demux_set_parent(_mod, _parent) _mod->__demux.parent = _parent

#define module_demux_init(_mod, _join_pid, _leave_pid)                                          \
    _mod->__demux.join_pid = _join_pid;                                                         \
    _mod->__demux.leave_pid = _leave_pid;                                                       \
    _mod->__demux.self = _mod

#define module_demux_destroy(_mod)                                                              \
    {                                                                                           \
        int __i;                                                                                \
        for(__i = 0; __i < MAX_PID; ++__i)                                                      \
        {                                                                                       \
            if(_mod->__demux.pid_list[__i])                                                     \
            {                                                                                   \
                demux_leave_pid(_mod, __i);                                                     \
            }                                                                                   \
        }                                                                                       \
    }

#define MODULE_DEMUX_METHODS()                                              \
    static int module_demux_demux(module_data_t *mod)                       \
    {                                                                       \
        lua_pushlightuserdata(lua, &mod->__demux);                          \
        return 1;                                                           \
    }

#define MODULE_DEMUX_METHODS_REF()                                          \
    { "demux", module_demux_demux }

#endif /* _MODULE_STREAM_H_ */
