/*
 * Astra DVB Module
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#include <astra.h>

#include <sys/socket.h>
#include <net/if.h>
#include <fcntl.h>
#include <dirent.h>

// #include <linux/dvb/version.h>
// #include <linux/dvb/net.h>

struct module_data_t
{
    int _empty;
};

static void module_init(module_data_t *mod)
{

}

static void module_destroy(module_data_t *mod)
{

}

MODULE_LUA_METHODS()
{
    { NULL, NULL }
};
MODULE_LUA_REGISTER(dvbls)
