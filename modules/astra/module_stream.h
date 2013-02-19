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
#include <core/asc.h>
#include <sys/queue.h>

typedef struct module_stream_s module_stream_t;

struct module_stream_s
{
    void (*on_ts)(module_data_t *mod, const uint8_t *ts);

    module_data_t *self;
    module_stream_t *parent;

    TAILQ_ENTRY(module_stream_s) entries;
    TAILQ_HEAD(list_s, module_stream_s) childs;
};

#define MODULE_STREAM_DATA() module_stream_t __stream

void __module_stream_init(module_stream_t *stream);
void __module_stream_destroy(module_stream_t *stream);

void __module_stream_attach(module_stream_t *stream, module_stream_t *child);
void __module_stream_detach(module_stream_t *stream, module_stream_t *child);

void __module_stream_send(module_stream_t *stream, const uint8_t *ts);

#define MODULE_STREAM_METHODS()                                             \
    static int module_stream_attach(module_data_t *mod)                     \
    {                                                                       \
        luaL_checktype(lua, 2, LUA_TLIGHTUSERDATA);                         \
        module_stream_t *child = lua_touserdata(lua, 2);                    \
        __module_stream_attach(&mod->__stream, child);                      \
        return 0;                                                           \
    }                                                                       \
    static int module_stream_detach(module_data_t *mod)                     \
    {                                                                       \
        luaL_checktype(lua, 2, LUA_TLIGHTUSERDATA);                         \
        module_stream_t *child = lua_touserdata(lua, 2);                    \
        __module_stream_detach(&mod->__stream, child);                      \
        return 0;                                                           \
    }                                                                       \
    static int module_stream_stream(module_data_t *mod)                     \
    {                                                                       \
        lua_pushlightuserdata(lua, &mod->__stream);                         \
        return 1;                                                           \
    }

#define module_stream_init(_mod, _on_ts)                                    \
    _mod->__stream.self = _mod;                                             \
    _mod->__stream.on_ts = _on_ts;                                          \
    __module_stream_init(&mod->__stream)

#define module_stream_destroy(_mod)                                         \
    __module_stream_destroy(&_mod->__stream)

#define module_stream_send(_mod, _ts)                                       \
    __module_stream_send(&_mod->__stream, _ts)

#define MODULE_STREAM_METHODS_REF()                                         \
    { "attach", module_stream_attach },                                     \
    { "detach", module_stream_detach },                                     \
    { "stream", module_stream_stream }

#endif /* _MODULE_STREAM_H_ */
