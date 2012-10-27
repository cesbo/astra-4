/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#include <astra.h>

#define LOG_MSG(_msg) "[timer] " _msg

struct module_data_s
{
    MODULE_BASE();

    struct
    {
        int interval;
    } config;

    int idx_cb;
    int idx_self;

    void *timer;
};

/* callbacks */

static void timer_callback(void *arg)
{
    module_data_t *mod = arg;
    lua_State *L = LUA_STATE(mod);
    lua_rawgeti(L, LUA_REGISTRYINDEX, mod->idx_cb);
    lua_rawgeti(L, LUA_REGISTRYINDEX, mod->idx_self);
    lua_call(L, 1, 0);
}

/* methods */

static int method_close(module_data_t *mod)
{
    lua_State *L = LUA_STATE(mod);

    if(mod->timer)
    {
        timer_detach(mod->timer);
        mod->timer = NULL;
    }

    luaL_unref(L, LUA_REGISTRYINDEX, mod->idx_self);
    mod->idx_self = 0;
    luaL_unref(L, LUA_REGISTRYINDEX, mod->idx_cb);
    mod->idx_cb = 0;

    return 0;
}

/* required */

static void module_init(module_data_t *mod)
{
    log_debug(LOG_MSG("init"));

    lua_State *L = LUA_STATE(mod);

    lua_pushvalue(L, -1);
    mod->idx_self = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_getfield(L, 2, "callback");
    luaL_checktype(L, -1, LUA_TFUNCTION);
    mod->idx_cb = luaL_ref(L, LUA_REGISTRYINDEX);

    if(!mod->config.interval)
    {
        log_error(LOG_MSG("interval must be greater than 0"));
        return;
    }

    mod->timer = timer_attach(mod->config.interval * 1000, timer_callback, mod);
}

static void module_destroy(module_data_t *mod)
{
    log_debug(LOG_MSG("destroy"));

    method_close(mod);
}

MODULE_OPTIONS()
{
    OPTION_NUMBER("interval", config.interval, 1, 0)
    OPTION_CUSTOM("callback", NULL           , 1)
};

MODULE_METHODS()
{
    METHOD(close)
};

MODULE(timer)
