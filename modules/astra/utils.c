/*
 * Astra Module: Utils
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
 * Set of the additional methods and classes for lua
 *
 * Methods:
 *      utils.hostname()
 *                  - get name of the host
 *      utils.ifaddrs()
 *                  - get network interfaces list (except Win32)
 *      utils.stat(path)
 *                  - file/folder information
 *      utils.readdir(path)
 *                  - iterator to scan directory located by path
 */

#include <astra.h>

#include <dirent.h>

#ifndef _WIN32
#   include <sys/socket.h>
#   include <ifaddrs.h>
#   include <netdb.h>
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

#ifdef WITH_IFADDRS
static int utils_ifaddrs(lua_State *L)
{
    struct ifaddrs *ifaddr;
    char host[NI_MAXHOST];

    const int s = getifaddrs(&ifaddr);
    asc_assert(s != -1, "getifaddrs() failed");

    static const char __ipv4[] = "ipv4";
    static const char __ipv6[] = "ipv6";
#ifdef AF_LINK
    static const char __link[] = "link";
#endif

    lua_newtable(L);

    for(struct ifaddrs *i = ifaddr; i; i = i->ifa_next)
    {
        if(!i->ifa_addr)
            continue;

        lua_getfield(L, -1, i->ifa_name);
        if(lua_isnil(L, -1))
        {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_pushstring(L, i->ifa_name);
            lua_pushvalue(L, -2);
            lua_settable(L, -4);
        }

        const int s = getnameinfo(i->ifa_addr, sizeof(struct sockaddr_in)
                                  , host, sizeof(host), NULL, 0
                                  , NI_NUMERICHOST);
        if(s == 0 && *host != '\0')
        {
            const char *ip_family = NULL;

            switch(i->ifa_addr->sa_family)
            {
                case AF_INET:
                    ip_family = __ipv4;
                    break;
                case AF_INET6:
                    ip_family = __ipv6;
                    break;
#ifdef AF_LINK
                case AF_LINK:
                    ip_family = __link;
                    break;
#endif
                default:
                    break;
            }

            if(ip_family)
            {
                int count = 0;
                lua_getfield(L, -1, ip_family);
                if(lua_isnil(L, -1))
                {
                    lua_pop(L, 1);
                    lua_newtable(L);
                    lua_pushstring(L, ip_family);
                    lua_pushvalue(L, -2);
                    lua_settable(L, -4);
                    count = 0;
                }
                else
                    count = luaL_len(L, -1);

                lua_pushnumber(L, count + 1);
                lua_pushstring(L, host);
                lua_settable(L, -3);
                lua_pop(L, 1);
            }
        }

        lua_pop(L, 1);
    }
    freeifaddrs(ifaddr);

    return 1;
}
#endif

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
#ifdef WITH_IFADDRS
        { "ifaddrs", utils_ifaddrs },
#endif
        { "stat", utils_stat },
        { NULL, NULL }
    };

    luaL_newlib(L, api);
    lua_pushvalue(L, -1);
    lua_setglobal(L, "utils");

    /* readdir */
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
