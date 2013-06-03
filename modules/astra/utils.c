/*
 * Astra Utils Module
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

/*
 * Set of the additional methods and classes for lua
 *
 * Methods:
 *      utils.hostname()
 *                  - get name of the host
 *      log.readdir(path)
 *                  - iterator to scan directory located by path
 */

#include <astra.h>

#include <dirent.h>

#ifdef _WIN32
#include <winsock2.h>
#endif

/* hostname */

static int utils_hostname(lua_State *L)
{
    char hostname[64];
    if(gethostname(hostname, sizeof(hostname)) != 0)
        luaL_error(L, "failed to get hostname");
    lua_pushstring(L, hostname);
    return 1;
}

static int utils_stat(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);

    lua_newtable(L);

    struct stat sb;
    if(stat(path, &sb) != 0)
    {
        lua_pushstring(L, strerror(errno));
        lua_setfield(L, -2, "error");

        memset(&sb, 0, sizeof(struct stat));
    }

    switch(sb.st_mode & S_IFMT)
    {
        case S_IFBLK: lua_pushstring(L, "block"); break;
        case S_IFCHR: lua_pushstring(L, "character"); break;
        case S_IFDIR: lua_pushstring(L, "directory"); break;
        case S_IFIFO: lua_pushstring(L, "pipe"); break;
        case S_IFREG: lua_pushstring(L, "file"); break;
#ifndef _WIN32
        case S_IFLNK: lua_pushstring(L, "symlink"); break;
        case S_IFSOCK: lua_pushstring(L, "socket"); break;
#endif
        default: lua_pushstring(L, "unknown"); break;
    }
    lua_setfield(L, -2, "type");

    lua_pushnumber(L, sb.st_uid);
    lua_setfield(L, -2, "uid");

    lua_pushnumber(L, sb.st_gid);
    lua_setfield(L, -2, "gid");

    lua_pushnumber(L, sb.st_size);
    lua_setfield(L, -2, "size");

    return 1;
}

/* sha1 */

static int utils_sha1(lua_State *L)
{
    const char *data = luaL_checkstring(L, 1);
    const int data_size = luaL_len(L, 1);

    sha1_ctx_t ctx;
    memset(&ctx, 0, sizeof(sha1_ctx_t));
    sha1_init(&ctx);
    sha1_update(&ctx, (uint8_t *)data, data_size);
    uint8_t digest[SHA1_DIGEST_SIZE];
    sha1_final(&ctx, digest);

    lua_pushlstring(lua, (char *)digest, sizeof(digest));
    return 1;
}

/* readdir */

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

/* utils */

LUA_API int luaopen_utils(lua_State *L)
{
    static const luaL_Reg api[] =
    {
        { "hostname", utils_hostname },
        { "stat", utils_stat },
        { "sha1", utils_sha1 },
        { NULL, NULL }
    };

    luaL_newlib(L, api);

    /* readdir */
    lua_pushvalue(L, -1);
    const int table = lua_gettop(L);
    luaL_newmetatable(L, __utils_readdir);
    lua_pushcfunction(L, utils_readder_gc);
    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1); // metatable
    lua_pushcfunction(L, utils_readdir_init);
    lua_setfield(L, table, "readdir");
    lua_pop(L, 1); // table

    /* utils */
    lua_setglobal(L, "utils");

    return 1;
}
