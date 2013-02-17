/*
 * Astra Log Module
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
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
            log_set_debug(lua_toboolean(L, -1));
        }
        else if(!strcmp(var, "filename"))
        {
            const char *val = luaL_checkstring(L, -1);
            log_set_file((*val != '\0') ? val : NULL);
        }
#ifndef _WIN32
        else if(!strcmp(var, "syslog"))
        {
            const char *val = luaL_checkstring(L, -1);
            log_set_syslog((*val != '\0') ? val : NULL);
        }
#endif
        else if(!strcmp(var, "stdout"))
        {
            luaL_checktype(L, -1, LUA_TBOOLEAN);
            log_set_stdout(lua_toboolean(L, -1));
        }
    }

    return 0;
}

static int lua_log_error(lua_State *L)
{
    log_error("%s", luaL_checkstring(L, 1));
    return 0;
}

static int lua_log_warning(lua_State *L)
{
    log_warning("%s", luaL_checkstring(L, 1));
    return 0;
}

static int lua_log_info(lua_State *L)
{
    log_info("%s", luaL_checkstring(L, 1));
    return 0;
}

static int lua_log_debug(lua_State *L)
{
    log_debug("%s", luaL_checkstring(L, 1));
    return 0;
}

LUA_API int luaopen_log(lua_State *L)
{
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
