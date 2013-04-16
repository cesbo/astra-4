/*
 * Astra Methods
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

/*
 * Set of the astra methods and variables for lua
 *
 * Variables:
 *      astra.version
 *                  - string, astra version string
 *      astra.debug - boolean, is a debug version
 *
 * Methods:
 *      astra.abort()
 *                  - abort execution
 *      astra.exit()
 *                  - normal exit from astra
 */

#include <astra.h>

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

LUA_API int luaopen_astra(lua_State *L)
{
    static luaL_Reg astra_api[] =
    {
        { "exit", _astra_exit },
        { "abort", _astra_abort },
        { NULL, NULL }
    };

    luaL_newlib(L, astra_api);

    lua_pushboolean(lua,
#ifdef DEBUG
                    1
#else
                    0
#endif
                    );

    lua_setfield(lua, -2, "debug");

    lua_pushstring(lua, ASTRA_VERSION_STR);
    lua_setfield(lua, -2, "version");

    lua_setglobal(L, "astra");

    return 1;
}
