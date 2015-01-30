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

#ifndef _WIN32
#   include <signal.h>
#endif

#include <setjmp.h>

#include "config.h"

jmp_buf main_loop;
bool is_main_loop_idle = true;
bool is_sighup = false;

void astra_exit(void)
{
#ifndef _WIN32
    longjmp(main_loop, 1);
#else
    exit(0);
#endif
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

void astra_reload(void)
{
    longjmp(main_loop, 2);
}

#ifndef _WIN32
static void signal_handler(int signum)
{
    switch(signum)
    {
        case SIGHUP:
            asc_log_hup();
            is_sighup = true;
            return;
        case SIGPIPE:
            return;
        default:
            astra_exit();
    }
}
#else
static bool WINAPI signal_handler(DWORD signum)
{
    switch(signum)
    {
        case CTRL_C_EVENT:
            astra_exit();
            break;
        case CTRL_BREAK_EVENT:
            astra_exit();
            break;
        default:
            break;
    }
    return true;
}
#endif

static void asc_srand()
{
    unsigned long a = clock();
    unsigned long b = time(NULL);
#ifndef _WIN32
    unsigned long c = getpid();
#else
    unsigned long c = GetCurrentProcessId();
#endif

    a = a - b;  a = a - c;  a = a ^ (c >> 13);
    b = b - c;  b = b - a;  b = b ^ (a << 8);
    c = c - a;  c = c - b;  c = c ^ (b >> 13);
    a = a - b;  a = a - c;  a = a ^ (c >> 12);
    b = b - c;  b = b - a;  b = b ^ (a << 16);
    c = c - a;  c = c - b;  c = c ^ (b >> 5);
    a = a - b;  a = a - c;  a = a ^ (c >> 3);
    b = b - c;  b = b - a;  b = b ^ (a << 10);
    c = c - a;  c = c - b;  c = c ^ (b >> 15);

    srand(c);
}

int main(int argc, const char **argv)
{
#ifndef _WIN32
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, signal_handler);
    signal(SIGHUP, signal_handler);
    signal(SIGQUIT, signal_handler);
#else
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)signal_handler, true);
#endif

astra_reload_entry:

    asc_srand();
    asc_thread_core_init();
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

#ifndef _WIN32
#   define ASC_PATH_SEP "/"
#else
#   define ASC_PATH_SEP "\\"
#endif

    lua_pushfstring(lua, "." ASC_PATH_SEP "?.lua");
    lua_setfield(lua, -2, "path");
    lua_pushstring(lua, "");
    lua_setfield(lua, -2, "cpath");
    lua_pop(lua, 1);

    /* argv table */
    lua_newtable(lua);
    for(int i = 1; i < argc; ++i)
    {
        lua_pushinteger(lua, i);
        lua_pushstring(lua, argv[i]);
        lua_settable(lua, -3);
    }
    lua_setglobal(lua, "argv");

#define GC_TIMEOUT (1 * 1000 * 1000)

    uint64_t current_time = asc_utime();
    uint64_t gc_check_timeout = current_time;

    /* start */
    const int main_loop_status = setjmp(main_loop);
    if(main_loop_status == 0)
    {
        lua_getglobal(lua, "inscript");
        if(lua_isfunction(lua, -1))
        {
            lua_call(lua, 0, 0);
        }
        else
        {
            lua_pop(lua, 1);

            if(argc < 2)
            {
                printf("Astra " ASTRA_VERSION_STR);
                printf("Usage: %s script.lua [OPTIONS]\n", argv[0]);
                astra_exit();
            }

            int ret = -1;

            if(argv[1][0] == '-' && argv[1][1] == 0)
                ret = luaL_dofile(lua, NULL);
            else if(!access(argv[1], R_OK))
                ret = luaL_dofile(lua, argv[1]);
            else
            {
                printf("Error: initial script isn't found\n");
                astra_exit();
            }

            if(ret != 0)
                luaL_error(lua, "[main] %s", lua_tostring(lua, -1));
        }

        while(true)
        {
            is_main_loop_idle = true;

            asc_event_core_loop();
            asc_timer_core_loop();
            asc_thread_core_loop();

            if(is_sighup)
            {
                is_sighup = false;

                lua_getglobal(lua, "on_sighup");
                if(lua_isfunction(lua, -1))
                {
                    lua_call(lua, 0, 0);
                    is_main_loop_idle = false;
                }
                else
                    lua_pop(lua, 1);
            }

            if(is_main_loop_idle)
            {
                current_time = asc_utime();
                if((current_time - gc_check_timeout) >= GC_TIMEOUT)
                {
                    gc_check_timeout = current_time;
                    lua_gc(lua, LUA_GCCOLLECT, 0);
                }

                asc_usleep(1000);
            }
        }
    }

    /* destroy */
    lua_close(lua);

    asc_event_core_destroy();
    asc_socket_core_destroy();
    asc_timer_core_destroy();
    asc_thread_core_destroy();

    asc_log_info("[main] %s", (main_loop_status == 2) ? "reload" : "exit");
    asc_log_core_destroy();

    if(main_loop_status == 2)
        goto astra_reload_entry;

    return 0;
}
