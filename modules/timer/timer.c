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

static void module_configure(module_data_t *mod)
{
    module_set_number(mod, "interval", 1, 0, &mod->config.interval);
    if(mod->config.interval <= 0)
    {
        log_error(LOG_MSG("option \"interval\" must be greater than 0"));
        abort();
    }

    lua_State *L = LUA_STATE(mod);
    lua_getfield(L, 2, "callback");
    if(lua_type(L, -1) != LUA_TFUNCTION)
    {
        log_error(LOG_MSG("option \"callback\" must be a function"));
        abort();
    }
    luaL_checktype(L, -1, LUA_TFUNCTION);
    mod->idx_cb = luaL_ref(L, LUA_REGISTRYINDEX);

}
static void module_initialize(module_data_t *mod)
{
    module_configure(mod);

    lua_State *L = LUA_STATE(mod);

    lua_pushvalue(L, -1);
    mod->idx_self = luaL_ref(L, LUA_REGISTRYINDEX);

    mod->timer = timer_attach(mod->config.interval * 1000
                              , timer_callback, mod);
}

static void module_destroy(module_data_t *mod)
{
    method_close(mod);
}

MODULE_METHODS()
{
    METHOD(close)
};

MODULE(timer)
