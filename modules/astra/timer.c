/*
 * Astra Timer Module
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

/*
 * Module Name:
 *      timer
 *
 * Module Options:
 *      interval    - number, sets the interval between triggers, in seconds
 *      callback    - function, handler is called when the timer is triggered
 */

#include <astra.h>

struct module_data_t
{
    MODULE_LUA_DATA();

    asc_timer_t *timer;
};

/* callbacks */

static void timer_callback(void *arg)
{
    module_data_t *mod = arg;
    lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->__lua.oref);
    lua_getfield(lua, -1, "callback");
    lua_call(lua, 0, 0);
    lua_pop(lua, 1); // options
}

/* required */

static void module_init(module_data_t *mod)
{
    int interval = 0;
    if(!module_option_number("interval", &interval) || interval <= 0)
    {
        asc_log_error("[timer] option 'interval' is required and must be reater than 0");
        astra_abort();
    }

    lua_getfield(lua, 2, "callback");
    if(lua_type(lua, -1) != LUA_TFUNCTION)
    {
        asc_log_error("[timer] option 'callback' must be a function");
        astra_abort();
    }
    lua_pop(lua, 1);

    mod->timer = asc_timer_init(interval * 1000, timer_callback, mod);
}

static void module_destroy(module_data_t *mod)
{
    if(mod->timer)
    {
        asc_timer_destroy(mod->timer);
        mod->timer = NULL;
    }
}

MODULE_LUA_METHODS()
{
    { NULL, NULL }
};
MODULE_LUA_REGISTER(timer)
