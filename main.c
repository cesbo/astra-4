/*
 * Astra
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#include <astra.h>

#include <signal.h>
#include <sys/stat.h>
#include <setjmp.h>

#include "config.h"

static const char __version[] = ASTRA_VERSION_STR;
static jmp_buf main_loop;

lua_State *lua;

void astra_exit(void)
{
    longjmp(main_loop, 1);
}

void astra_abort(void)
{
    lua_Debug ar;
    lua_getstack(lua, 1, &ar);
    lua_getinfo(lua, "nSl", &ar);
    log_error("[main] abort execution. line:%d source:%s", ar.currentline, ar.source);
    abort();
}

static int _astra_exit(lua_State *L)
{
    (void)L;
    astra_exit();
    return 0;
}

static int _astra_abort(lua_State *L)
{
    (void)L;
    astra_abort();
    return 0;
}

static luaL_Reg astra_api[] =
{
    { "exit", _astra_exit },
    { "abort", _astra_abort },
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

    static int is_signum = 0;
    if(is_signum)
        return;
    is_signum = 1;
    longjmp(main_loop, 1);
}

static void astra_init(int argc, const char **argv)
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#ifndef _WIN32
    signal(SIGHUP, signal_handler);
    signal(SIGQUIT, signal_handler);
#endif

    ASC_INIT();

    lua = luaL_newstate();
    luaL_openlibs(lua);

    // astra
    luaL_newlib(lua, astra_api);

#ifdef DEBUG
    lua_pushboolean(lua, 1);
#else
    lua_pushboolean(lua, 0);
#endif
    lua_setfield(lua, -2, "debug");

    lua_pushstring(lua, __version);
    lua_setfield(lua, -2, "version");

    lua_setglobal(lua, "astra");

    for(int i = 0; astra_mods[i]; i++)
        astra_mods[i](lua);

    /* argv table */
    lua_newtable(lua);
    for(int i = 0; i < argc; i++)
    {
        lua_pushinteger(lua, i + 1);
        lua_pushstring(lua, argv[i]);
        lua_settable(lua, -3);
    }
    lua_setglobal(lua, "argv");

    /* change package.path */
    lua_getglobal(lua, "package");
    lua_pushstring(lua, "/etc/astra/helpers/?.lua;./?.lua");
    lua_setfield(lua, -2, "path");
    lua_pushstring(lua, "");
    lua_setfield(lua, -2, "cpath");
    lua_pop(lua, 1);
}

void astra_do_file(int argc, const char **argv, const char *file)
{
    if(file[0] == '-' && file[1] == '\0')
        file = NULL;
    else if(!access(file, R_OK))
        ;
    else
    {
        printf("Error: initial script isn't found [%s]\n", strerror(errno));
        return;
    }

    astra_init(argc, argv);

    if(!setjmp(main_loop))
    {
        if(luaL_dofile(lua, file))
            luaL_error(lua, "[main] %s", lua_tostring(lua, -1));
        ASC_LOOP();
    }

    lua_close(lua);
    ASC_DESTROY();
}

void astra_do_text(int argc, const char **argv, const char *text, size_t size)
{
    astra_init(argc, argv);

    if(!setjmp(main_loop))
    {
        if(luaL_loadbuffer(lua, text, size, "=inscript")
           || lua_pcall(lua, 0, LUA_MULTRET, 0))
        {
            luaL_error(lua, "[main] %s", lua_tostring(lua, -1));
        }
        ASC_LOOP();
    }

    lua_close(lua);
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

    /* 2 - skip app and script names */
    astra_do_file(argc - 2, argv + 2, argv[1]);

    return 0;
}
#endif /* ! ASTRA_SHELL */
