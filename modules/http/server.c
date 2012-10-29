/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#include <astra.h>

#define LOG_MSG(_msg) "[http_server %s:%d] " _msg \
        , mod->config.addr, mod->config.port

struct module_data_s
{
    MODULE_BASE();

    struct
    {
        const char *addr;
        int port;
    } config;

    int idx_self;

    int sock;
};

static const char __callback[] = "callback";

/* callbacks */

static int method_close(module_data_t *);

static void accept_callback(void *arg, int event)
{
    module_data_t *mod = arg;

    if(event == EVENT_ERROR)
    {
        method_close(mod);
        return;
    }

    char addr[16];
    int port;
    int csock = socket_accept(mod->sock, addr, &port);
    if(!csock)
        return;

    lua_State *L = LUA_STATE(mod);

    lua_rawgeti(L, LUA_REGISTRYINDEX, mod->__idx_options);
    const int options = lua_gettop(L);

    lua_getglobal(L, "http_client");
    lua_newtable(L);
    lua_getfield(L, options, __callback);
    lua_setfield(L, -2, __callback);
    lua_pushnumber(L, csock);
    lua_setfield(L, -2, "fd");
    lua_rawgeti(L, LUA_REGISTRYINDEX, mod->idx_self);
    lua_setfield(L, -2, "server");
    lua_pushstring(L, addr);
    lua_setfield(L, -2, "addr");
    lua_pushnumber(L, port);
    lua_setfield(L, -2, "port");
    lua_call(L, 1, 0);

    lua_pop(L, 1); // options
}

/* methods */

static int method_close(module_data_t *mod)
{
    if(mod->sock > 0)
    {
        event_detach(mod->sock);
        socket_close(mod->sock);
        mod->sock = 0;
    }

    if(mod->idx_self > 0)
    {
        lua_State *L = LUA_STATE(mod);
        luaL_unref(L, LUA_REGISTRYINDEX, mod->idx_self);
        mod->idx_self = 0;
    }

    return 0;
}

static int method_port(module_data_t *mod)
{
    int port = socket_port(mod->sock);
    lua_State *L = LUA_STATE(mod);
    lua_pushnumber(L, port);
    return 1;
}

/* required */

static void module_configure(module_data_t *mod)
{
    /*
     * OPTIONS:
     *   addr, port, callback
     */

    module_set_string(mod, "addr", 0, "0.0.0.0", &mod->config.addr);
    module_set_number(mod, "port", 0, 80, &mod->config.port);

    lua_State *L = LUA_STATE(mod);
    lua_getfield(L, 2, "callback");
    if(lua_type(L, -1) != LUA_TFUNCTION)
    {
        log_error(LOG_MSG("option \"callback\" is required"));
        abort();
    }
    lua_pop(L, 1);
}

static void module_initialize(module_data_t *mod)
{
    lua_State *L = LUA_STATE(mod);

    // store self in registry
    lua_pushvalue(L, -1);
    mod->idx_self = luaL_ref(L, LUA_REGISTRYINDEX);

    mod->sock = socket_open(SOCKET_BIND | SOCKET_REUSEADDR
                            , mod->config.addr, mod->config.port);
    if(!mod->sock)
        method_close(mod);
    event_attach(mod->sock, accept_callback, mod, EVENT_READ);
    log_debug(LOG_MSG("fd=%d"), mod->sock);
}

static void module_destroy(module_data_t *mod)
{
    method_close(mod);
}

MODULE_METHODS()
{
    METHOD(close)
    METHOD(port)
};

MODULE(http_server)
