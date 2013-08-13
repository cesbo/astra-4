/*
 * Astra Module: DVB
 * http://cesbo.com/en/astra
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

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <fcntl.h>
#include <dirent.h>

#include "dvb.h"
#include <linux/dvb/net.h>

static int count;
static char dev_name[512];

static int adapter;
static int device;

static const char __adapter[] = "adapter";
static const char __device[] = "device";

static void iterate_dir(const char *dir, const char *filter, void (*callback)(const char *))
{
    DIR *dirp = opendir(dir);
    if(!dirp)
    {
        printf("ERROR: opendir() failed %s [%s]\n", dir, strerror(errno));
        return;
    }

    char item[64];
    const int item_len = sprintf(item, "%s/", dir);
    const int filter_len = strlen(filter);
    do
    {
        struct dirent *entry = readdir(dirp);
        if(!entry)
            break;
        if(strncmp(entry->d_name, filter, filter_len))
            continue;
        sprintf(&item[item_len], "%s", entry->d_name);
        callback(item);
    } while(1);

    closedir(dirp);
}

static int get_last_int(const char *str)
{
    int i = 0;
    int i_pos = -1;
    for(; str[i]; ++i)
    {
        const char c = str[i];
        if(c >= '0' && c <= '9')
        {
            if(i_pos == -1)
                i_pos = i;
        }
        else if(i_pos >= 0)
            i_pos = -1;
    }

    if(i_pos == -1)
        return 0;

    return atoi(&str[i_pos]);
}

static void check_device_net(void)
{
    sprintf(dev_name, "/dev/dvb/adapter%d/net%d", adapter, device);

    int fd = open(dev_name, O_RDWR | O_NONBLOCK);
    static char dvb_mac[] = "00:00:00:00:00:00";

    do
    {
        if(fd <= 0)
        {
            lua_pushfstring(lua, "failed to open [%s]", strerror(errno));
            break;
        }

        struct dvb_net_if net =
        {
            .pid = 0,
            .if_num = 0,
            .feedtype = 0
        };
        if(ioctl(fd, NET_ADD_IF, &net) != 0)
        {
            lua_pushfstring(lua, "NET_ADD_IF failed [%s]", strerror(errno));
            break;
        }

        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        sprintf(ifr.ifr_name, "dvb%d_%d", adapter, device);

        int sock = socket(PF_INET, SOCK_DGRAM, 0);
        if(ioctl(sock, SIOCGIFHWADDR, &ifr) != 0)
            lua_pushfstring(lua, "SIOCGIFHWADDR failed [%s]", strerror(errno));
        else
        {
            const uint8_t *mac = (uint8_t *)ifr.ifr_hwaddr.sa_data;
            sprintf(dvb_mac, "%02X:%02X:%02X:%02X:%02X:%02X"
                    , mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            lua_pushstring(lua, dvb_mac);
        }
        close(sock);

        if(ioctl(fd, NET_REMOVE_IF, net.if_num) != 0)
        {
            lua_pop(lua, 1);
            lua_pushfstring(lua, "NET_REMOVE_IF failed [%s]", strerror(errno));
        }
    } while(0);

    if(fd > 0)
        close(fd);

    lua_setfield(lua, -2, "mac");
}

static void check_device_fe(void)
{
    sprintf(dev_name, "/dev/dvb/adapter%d/frontend%d", adapter, device);

    bool is_busy = false;

    int fd = open(dev_name, O_RDWR | O_NONBLOCK);
    if(fd <= 0)
    {
        is_busy = true;
        fd = open(dev_name, O_RDONLY | O_NONBLOCK);
    }

    if(fd <= 0)
    {
        lua_pushfstring(lua, "failed to open [%s]", strerror(errno));
        lua_setfield(lua, -2, "error");
        return;
    }

    lua_pushboolean(lua, is_busy);
    lua_setfield(lua, -2, "busy");

    struct dvb_frontend_info feinfo;
    if(ioctl(fd, FE_GET_INFO, &feinfo) != 0)
        lua_pushstring(lua, "unknown");
    else
        lua_pushstring(lua, feinfo.name);
    close(fd);
    lua_setfield(lua, -2, "frontend");

    check_device_net();
}

static void check_device(const char *item)
{
    device = get_last_int(&item[(sizeof("/dev/dvb/adapter") - 1) + (sizeof("/net") - 1)]);

    lua_newtable(lua);
    lua_pushnumber(lua, adapter);
    lua_setfield(lua, -2, __adapter);
    lua_pushnumber(lua, device);
    lua_setfield(lua, -2, __device);
    check_device_fe();

    ++count;
    lua_rawseti(lua, -2, count);
}

static void check_adapter(const char *item)
{
    adapter = get_last_int(&item[sizeof("/dev/dvb/adapter") - 1]);
    iterate_dir(item, "net", check_device);
}

static int dvbls_scan(lua_State *L)
{
    __uarg(L);

    count = 0;
    lua_newtable(lua);
    iterate_dir("/dev/dvb", __adapter, check_adapter);
    return 1;
}

LUA_API int luaopen_dvbls(lua_State *L)
{
    lua_register(L, "dvbls", dvbls_scan);
    return 1;
}
