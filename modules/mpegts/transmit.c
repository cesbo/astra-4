/*
 * Astra Simple Transmit module API
 * http://cesbo.com/astra
 *
 * Copyright (C) 2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

/*
 * Module Name:
 *      transmit
 *
 * Module Options:
 *      upstream    - object, stream instance returned by module_instance:stream()
 */

#include <astra.h>

struct module_data_t
{
    MODULE_LUA_DATA();
    MODULE_STREAM_DATA();
};

static int method_set_upstream(module_data_t *mod)
{
    if(lua_type(lua, 2) != LUA_TLIGHTUSERDATA)
        __module_stream_attach(lua_touserdata(lua, 2), &mod->__stream);
    return 0;
}

static void on_ts(module_data_t *mod, const uint8_t *ts)
{
    module_stream_send(mod, ts);
}

static void module_init(module_data_t *mod)
{
    module_stream_init(mod, on_ts);
}

static void module_destroy(module_data_t *mod)
{
    module_stream_destroy(mod);
}

MODULE_STREAM_METHODS()
MODULE_LUA_METHODS()
{
    { "set_upstream", method_set_upstream },
    MODULE_STREAM_METHODS_REF()
};

MODULE_LUA_REGISTER(transmit)
