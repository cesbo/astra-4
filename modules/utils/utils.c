/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#include <astra.h>

#ifdef _WIN32
#include <winsock2.h>
#endif

static int utils_hostname(lua_State *L)
{
    char hostname[64];
    if(gethostname(hostname, sizeof(hostname)) != 0)
        luaL_error(L, "failed to get hostname");
    lua_pushstring(L, hostname);
    return 1;
}

/* readdir */

extern int luaopen_utils_readdir(lua_State *, int);

LUA_API int luaopen_utils(lua_State *L)
{
    static const luaL_Reg api[] =
    {
        { "hostname", utils_hostname },
        { NULL, NULL }
    };

    luaL_newlib(L, api);

    luaopen_utils_readdir(L, -1);

    lua_setglobal(L, "utils");

    return 1;
}
