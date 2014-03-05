/*
 * Astra Module: Log
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
 * Set of the logging methods for lua
 *
 * Methods:
 *      log.set({ options })
 *                  - set logging options:
 *                    debug     - boolean, allow debug messages, false by default
 *                    filename  - string, writing log to a file
 *                    syslog    - string, sending log to the syslog,
 *                                is not available under the windows
 *                    stdout    - boolean, writing log to the stdout, true by default
 *      log.error(message)
 *                  - error message
 *      log.warning(message)
 *                  - warning message
 *      log.info(message)
 *                  - information message
 *      log.debug(message)
 *                  - debug message
 */

#include <astra.h>

static bool is_debug = false;

static int lua_log_set(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);

    // store in registry to prevent the gc cleaning
    lua_pushstring(L, "astra.log");
    lua_pushvalue(L, 1);
    lua_settable(L, LUA_REGISTRYINDEX);


    for(lua_pushnil(L); lua_next(L, 1); lua_pop(L, 1))
    {
        const char *var = lua_tostring(L, -2);

        if(!strcmp(var, "debug"))
        {
            luaL_checktype(L, -1, LUA_TBOOLEAN);
            is_debug = lua_toboolean(L, -1);
            asc_log_set_debug(is_debug);
        }
        else if(!strcmp(var, "filename"))
        {
            const char *val = luaL_checkstring(L, -1);
            asc_log_set_file((*val != '\0') ? val : NULL);
        }
#ifndef _WIN32
        else if(!strcmp(var, "syslog"))
        {
            const char *val = luaL_checkstring(L, -1);
            asc_log_set_syslog((*val != '\0') ? val : NULL);
        }
#endif
        else if(!strcmp(var, "stdout"))
        {
            luaL_checktype(L, -1, LUA_TBOOLEAN);
            asc_log_set_stdout(lua_toboolean(L, -1));
        }
    }

    return 0;
}

static int lua_log_error(lua_State *L)
{
    asc_log_error("%s", luaL_checkstring(L, 1));
    return 0;
}

static int lua_log_warning(lua_State *L)
{
    asc_log_warning("%s", luaL_checkstring(L, 1));
    return 0;
}

static int lua_log_info(lua_State *L)
{
    asc_log_info("%s", luaL_checkstring(L, 1));
    return 0;
}

static int lua_log_debug(lua_State *L)
{
    if(is_debug)
        asc_log_debug("%s", luaL_checkstring(L, 1));
    return 0;
}

LUA_API int luaopen_log(lua_State *L)
{
    is_debug = asc_log_is_debug();

    static const luaL_Reg api[] =
    {
        { "set", lua_log_set },
        { "error", lua_log_error },
        { "warning", lua_log_warning },
        { "info", lua_log_info },
        { "debug", lua_log_debug },
        { NULL, NULL }
    };

    luaL_newlib(L, api);
    lua_setglobal(L, "log");

    return 1;
}
