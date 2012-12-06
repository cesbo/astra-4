/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#include <astra.h>

#include <signal.h>
#include <sys/stat.h>
#include <setjmp.h>

#include "config.h"

static const char __version[] = ASTRA_VERSION_STR;
static jmp_buf main_loop;

static int astra_exit(lua_State *L)
{
    longjmp(main_loop, 1);
    return 0;
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

    longjmp(main_loop, 1);
}

void astra_main(int argc, const char **argv, const char *lua_script)
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#ifndef _WIN32
    signal(SIGHUP, signal_handler);
    signal(SIGQUIT, signal_handler);
#endif

    ASC_INIT();

    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    // astra
    luaL_newlib(L, astra_api);

#ifdef DEBUG
    lua_pushboolean(L, 1);
#else
    lua_pushboolean(L, 0);
#endif
    lua_setfield(L, -2, "debug");

    lua_pushstring(L, __version);
    lua_setfield(L, -2, "version");

    lua_setglobal(L, "astra");

    for(int i = 0; astra_mods[i]; i++)
        astra_mods[i](L);

    /* argv table */
    lua_newtable(L);
    for(int i = 0; i < argc; i++)
    {
        lua_pushinteger(L, i + 1);
        lua_pushstring(L, argv[i]);
        lua_settable(L, -3);
    }
    lua_setglobal(L, "argv");

    /* change package.path */
    lua_getglobal(L, "package");
    lua_pushstring(L, "/etc/astra/helpers/?.lua;./?.lua");
    lua_setfield(L, -2, "path");
    lua_pushstring(L, "");
    lua_setfield(L, -2, "cpath");
    lua_pop(L, 1);

    if(!setjmp(main_loop))
    {
        if(luaL_dofile(L, lua_script))
            luaL_error(L, "[main] %s", lua_tostring(L, -1));
        ASC_LOOP();
    }

    lua_close(L);
    ASC_DESTROY();
}

#ifndef ASTRA_SHELL
int main(int argc, const char **argv)
{
    if(argc < 2)
    {
        printf("Astra %s\n"
               "Usage: %s script [argv]\n"
               , __version, argv[0]);
        return 1;
    }
    const char *lua_script = argv[1];
    if(lua_script[0] == '-' && lua_script[1] == '\0')
    {
        lua_script = NULL;
    }
    else if(!access(lua_script, R_OK))
    {
        ;
    }
    else
    {
        printf("Error: initial script isn't found\n");
        return 1;
    }

    /* 2 - skip app and script names */
    astra_main(argc - 2, argv + 2, lua_script);

    return 0;
}
#endif /* ! ASTRA_SHELL */
