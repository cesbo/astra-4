/*
 * Astra Event Dispatcher API
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#ifndef _MODULE_EVENT_H_
#define _MODULE_EVENT_H_ 1

#define MODULE_EVENT_BASE()                                                 \
struct                                                                      \
{                                                                           \
    int idx_cb;                                                             \
    int idx_self;                                                           \
} __module_event

#define __module_event_unset(_L, _mod)                                      \
    if(_mod->__module_event.idx_cb)                                         \
    {                                                                       \
        luaL_unref(_L, LUA_REGISTRYINDEX, _mod->__module_event.idx_cb);     \
        luaL_unref(_L, LUA_REGISTRYINDEX, _mod->__module_event.idx_self);   \
        _mod->__module_event.idx_cb = 0;                                    \
        _mod->__module_event.idx_self = 0;                                  \
    }

#define module_event_set(_mod)                                              \
{                                                                           \
    lua_State *__L = LUA_STATE(_mod);                                       \
    __module_event_unset(__L, _mod);                                        \
    if(lua_gettop(__L) == 2 && lua_type(__L, 2) == LUA_TFUNCTION)           \
    {                                                                       \
        lua_pushvalue(__L, 2);                                              \
        _mod->__module_event.idx_cb = luaL_ref(__L, LUA_REGISTRYINDEX);     \
        lua_pushvalue(__L, 1);                                              \
        _mod->__module_event.idx_self = luaL_ref(__L, LUA_REGISTRYINDEX);   \
    }                                                                       \
}

#define module_event_call(_mod)                                             \
{                                                                           \
    if(_mod->__module_event.idx_cb)                                         \
    {                                                                       \
        lua_State *__L = LUA_STATE(_mod);                                   \
        lua_rawgeti(__L, LUA_REGISTRYINDEX, _mod->__module_event.idx_cb);   \
        lua_rawgeti(__L, LUA_REGISTRYINDEX, _mod->__module_event.idx_self); \
        lua_call(__L, 1, 0);                                                \
    }                                                                       \
}

#define module_event_destroy(_mod)                                          \
{                                                                           \
    lua_State *__L = LUA_STATE(_mod);                                       \
    __module_event_unset(__L, _mod);                                        \
}

#endif /* _MODULE_EVENT_H_ */
