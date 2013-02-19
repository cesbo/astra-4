/*
 * Astra Module Stream API
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#include <astra.h>

struct module_data_s
{
    MODULE_STREAM_DATA();
};

static void __module_stream_detach(module_data_t *parent, module_data_t *child)
{
    module_data_t *i;
    TAILQ_FOREACH(i, &parent->__stream.childs, __stream.entries)
    {
        if(i == child)
        {
            TAILQ_REMOVE(&parent->__stream.childs, i, __stream.entries);
            break;
        }
    }
    child->__stream.parent = NULL;
}

static void __module_stream_attach(module_data_t *parent, module_data_t *child)
{
    if(child->__stream.parent)
        __module_stream_detach(child->__stream.parent, child);
    child->__stream.parent = parent;
    TAILQ_INSERT_TAIL(&parent->__stream.childs, child, __stream.entries);
}

int module_stream_attach(module_data_t *mod)
{
    luaL_checktype(lua, 2, LUA_TTABLE);
    module_data_t *child = lua_touserdata(lua, 2);
    __module_stream_attach(mod, child);
    return 0;
}

int module_stream_detach(module_data_t *mod)
{
    luaL_checktype(lua, 2, LUA_TTABLE);
    module_data_t *child = lua_touserdata(lua, 2);
    __module_stream_detach(mod, child);
    return 0;
}

void module_stream_send(module_data_t *mod, const uint8_t *ts)
{
    module_data_t *i;
    TAILQ_FOREACH(i, &mod->__stream.childs, __stream.entries)
        i->__stream.api.on_ts(i, ts);
}

void module_stream_init(module_data_t *mod, module_stream_api_t *api)
{
    if(api->on_ts == NULL)
        abort();
    mod->__stream.api.on_ts = api->on_ts;
    TAILQ_INIT(&mod->__stream.childs);
}

void module_stream_destroy(module_data_t *mod)
{
    if(mod->__stream.parent)
        __module_stream_detach(mod->__stream.parent, mod);
    module_data_t *i, *n;
    TAILQ_FOREACH_SAFE(i, &mod->__stream.childs, __stream.entries, n)
    {
        i->__stream.parent = NULL;
        TAILQ_REMOVE(&mod->__stream.childs, i, __stream.entries);
    }
}
