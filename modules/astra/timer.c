/*
 * Astra Module: Timer
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

    int idx_self;
    asc_timer_t *timer;
};

static void timer_callback(void *arg)
{
    module_data_t *mod = arg;
    lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->__lua.oref);
    lua_getfield(lua, -1, "callback");
    lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_self);
    lua_call(lua, 1, 0);
    lua_pop(lua, 1); // options
}

static int method_close(module_data_t *mod)
{
    if(mod->idx_self > 0)
    {
        luaL_unref(lua, LUA_REGISTRYINDEX, mod->idx_self);
        mod->idx_self = 0;
    }

    if(mod->timer)
    {
        asc_timer_destroy(mod->timer);
        mod->timer = NULL;
    }

    return 0;
}

static void module_init(module_data_t *mod)
{
    int interval = 0;
    if(!module_option_number("interval", &interval) || interval <= 0)
    {
        asc_log_error("[timer] option 'interval' is required and must be greater than 0");
        astra_abort();
    }

    lua_getfield(lua, 2, "callback");
    if(lua_type(lua, -1) != LUA_TFUNCTION)
    {
        asc_log_error("[timer] option 'callback' must be a function");
        astra_abort();
    }
    lua_pop(lua, 1);

    // store self in registry
    lua_pushvalue(lua, 3);
    mod->idx_self = luaL_ref(lua, LUA_REGISTRYINDEX);

    mod->timer = asc_timer_init(interval * 1000, timer_callback, mod);
}

static void module_destroy(module_data_t *mod)
{
    if(mod->idx_self)
        method_close(mod);
}

MODULE_LUA_METHODS()
{
    { "close", method_close }
};
MODULE_LUA_REGISTER(timer)
