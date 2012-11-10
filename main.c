/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#include <astra.h>

#include <signal.h>
#include <sys/stat.h>

#include "config.h"

static const char __version[] = ASTRA_VERSION_STR;
static int main_loop = 1;

static int astra_exit(lua_State *L)
{
    main_loop = 0;
    return 0;
}

static int astra_version(lua_State *L)
{
    lua_pushstring(L, __version);

    lua_newtable(L);
    lua_pushnumber(L, 1);
    lua_pushnumber(L, ASTRA_VERSION);
    lua_settable(L, -3);
    lua_pushnumber(L, 2);
    lua_pushnumber(L, ASTRA_VERSION_DEV);
    lua_settable(L, -3);

    return 2;
}

static int astra_abort(lua_State *L)
{
    lua_Debug ar;
    lua_getstack(L, 1, &ar);
    lua_getinfo(L, "nSl", &ar);
    log_error("[main] abort execution. line:%d source:%s"
              , ar.currentline, ar.source);
    abort();

    return 0;
}

static luaL_Reg astra_api[] =
{
    { "exit", astra_exit },
    { "version", astra_version },
    { "abort", astra_abort },
    { NULL, NULL }
};

static void signal_handler(int signum)
{
#ifndef _WIN32
    if(signum == SIGHUP)
    {
        log_hup();
        return;
    }
#endif

    main_loop = 0;
}

int main(int argc, const char *argv[])
{
    if(argc < 2)
    {
        printf("Astra %s\n"
               "Usage: %s script [argv]\n"
               , __version, argv[0]);
        return 1;
    }
    const char *lua_script = argv[1];
    if(access(lua_script, R_OK))
    {
        printf("Error: initial script isn't found\n");
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#ifndef _WIN32
    signal(SIGHUP, signal_handler);
    signal(SIGQUIT, signal_handler);
#endif

    ASTRA_CORE_INIT();

    // astra
    luaL_newlib(L, astra_api);

#ifdef DEBUG
    lua_pushboolean(L, 1);
#else
    lua_pushboolean(L, 0);
#endif
    lua_setfield(L, -2, "debug");

    lua_setglobal(L, "astra");

    for(int i = 0; astra_mods[i]; i++)
        astra_mods[i](L);

    /* argv table */
    lua_newtable(L);
    for(int j = 1, i = 2; i < argc; i++, j++)
    {
        lua_pushinteger(L, j);
        lua_pushstring(L, argv[i]);
        lua_settable(L, -3);
    }
    lua_setglobal(L, "argv");

    /* change package.path */
    lua_getglobal(L, "package");
    lua_pushstring(L, "/etc/astra/helpers/?.lua");
    lua_setfield(L, -2, "path");
    lua_pushstring(L, "");
    lua_setfield(L, -2, "cpath");
    lua_pop(L, 1);

    if(luaL_dofile(L, lua_script))
        luaL_error(L, "[main] %s", lua_tostring(L, -1));

    ASTRA_CORE_LOOP(main_loop != 0);
    ASTRA_CORE_DESTROY();

    return 0;
}
