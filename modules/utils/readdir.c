/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#include <astra.h>
#include <dirent.h>

static const char __utils_readdir[] = "__utils_readdir";

static int utils_readdir_iter(lua_State *L)
{
    DIR *dirp = *(DIR **)lua_touserdata(L, lua_upvalueindex(1));
    struct dirent *entry;
    do
    {
        entry = readdir(dirp);
    } while(entry && entry->d_name[0] == '.');

    if(!entry)
        return 0;

    lua_pushstring(L, entry->d_name);
    return 1;
}

static int utils_readdir_init(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    DIR *dirp = opendir(path);
    if(!dirp)
        luaL_error(L, "cannot open %s: %s", path, strerror(errno));

    DIR **d = (DIR **)lua_newuserdata(L, sizeof(DIR *));
    *d = dirp;

    luaL_getmetatable(L, __utils_readdir);
    lua_setmetatable(L, -2);

    lua_pushcclosure(L, utils_readdir_iter, 1);
    return 1;
}

static int utils_readder_gc(lua_State *L)
{
    DIR **dirpp = (DIR **)lua_touserdata(L, 1);
    if(*dirpp)
    {
        closedir(*dirpp);
        *dirpp = NULL;
    }
    return 0;
}

int luaopen_utils_readdir(lua_State *L, int idx)
{
    lua_pushvalue(L, idx);
    const int table = lua_gettop(L);

    luaL_newmetatable(L, __utils_readdir);
    lua_pushcfunction(L, utils_readder_gc);
    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1); // metatable

    lua_pushcfunction(L, utils_readdir_init);
    lua_setfield(L, table, "readdir");

    lua_pop(L, 1); // table
    return 0;
}
