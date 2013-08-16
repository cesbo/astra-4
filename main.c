/*
 * Astra Main App
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

#include <astra.h>

#include <signal.h>
#include <setjmp.h>

#include "config.h"

static jmp_buf main_loop;

void astra_exit(void)
{
    longjmp(main_loop, 1);
}

void astra_abort(void)
{
    asc_log_error("[main] abort execution. Lua backtrace:");

    lua_Debug ar;
    int level = 1;
    while(lua_getstack(lua, level, &ar))
    {
        lua_getinfo(lua, "nSl", &ar);
        asc_log_error("[main] %d: %s:%d -- %s [%s]"
                      , level, ar.short_src, ar.currentline
                      , (ar.name) ? ar.name : "<unknown>"
                      , ar.what);
        ++level;
    }

    abort();
}

static void signal_handler(int signum)
{
#ifndef _WIN32
    if(signum == SIGHUP)
    {
        asc_log_hup();
        return;
    }
    if(signum == SIGPIPE)
        return;
#else
    __uarg(signum);
#endif

    astra_exit();
}

#ifdef INLINE_SCRIPT
extern int exec_script(int argc, const char **argv);
#else
static int exec_script(int argc, const char **argv)
{
    __uarg(argc);
    if(argv[1][0] == '-' && argv[1][1] == '\0')
        return luaL_dofile(lua, NULL);
    else if(!access(argv[1], R_OK))
        return luaL_dofile(lua, argv[1]);
    else
    {
        printf("Error: initial script isn't found [%s]\n", strerror(errno));
        return -1;
    }
}
#endif

int main(int argc, const char **argv)
{
#ifdef INLINE_SCRIPT
    int argv_skip = 1;
#else
    int argv_skip = 2;
#endif

    if(argc < argv_skip)
    {
        printf("Astra " ASTRA_VERSION_STR "\n"
               "Usage: %s script [argv]\n"
               , argv[0]);
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#ifndef _WIN32
    signal(SIGPIPE, signal_handler);
    signal(SIGHUP, signal_handler);
    signal(SIGQUIT, signal_handler);
#endif

    srand((uint32_t)time(NULL));
    asc_timer_core_init();
    asc_socket_core_init();
    asc_event_core_init();

    lua = luaL_newstate();
    luaL_openlibs(lua);

    /* load modules */
    for(int i = 0; astra_mods[i]; i++)
        astra_mods[i](lua);

    /* change package.path */
    lua_getglobal(lua, "package");
    lua_pushfstring(lua, "./?.lua;/etc/astra/scripts-%d.%d/?.lua"
                    , ASTRA_VERSION_MAJOR, ASTRA_VERSION_MINOR);
    lua_setfield(lua, -2, "path");
    lua_pushstring(lua, "");
    lua_setfield(lua, -2, "cpath");
    lua_pop(lua, 1);

    /* argv table */
    lua_newtable(lua);
    for(int i = 1; argv_skip < argc; ++argv_skip, ++i)
    {
        lua_pushinteger(lua, i);
        lua_pushstring(lua, argv[argv_skip]);
        lua_settable(lua, -3);
    }
    lua_setglobal(lua, "argv");

    /* start */
    if(!setjmp(main_loop))
    {
        if(exec_script(argc, argv) != 0)
            luaL_error(lua, "[main] %s", lua_tostring(lua, -1));

        while(true)
        {
            asc_event_core_loop();
            asc_timer_core_loop();
        }
    }

    /* destroy */
    lua_close(lua);
    asc_event_core_destroy();
    asc_socket_core_destroy();
    asc_timer_core_destroy();
    asc_log_info("[main] exit");
    asc_log_core_destroy();

    return 0;
}
