/*
 * Astra PID-file Module
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

/*
 * Not standard module. As initialize option uses string - path to the pid-file
 *
 * Module Name:
 *      pidfile
 *
 * Usage:
 *      pidfile("/path/to/file.pid")
 */

#ifdef _WIN32
#   error "pidfile module is not for win32"
#else

#include <astra.h>
#include <sys/stat.h>

struct module_data_t
{
    int idx_self;
};

static const char *filename = NULL;

/* required */

static void module_init(module_data_t *mod)
{
    __uarg(mod);

    if(filename)
    {
        asc_log_error("[pidfile] already created in %s", filename);
        astra_abort();
    }

    filename = luaL_checkstring(lua, MODULE_OPTIONS_IDX);

    if(!access(filename, W_OK))
        unlink(filename);

    static char tmp_pidfile[256];
    snprintf(tmp_pidfile, sizeof(tmp_pidfile), "%s.XXXXXX", filename);
    int fd = mkstemp(tmp_pidfile);
    if(fd == -1)
    {
        asc_log_error("[pidfile %s] mkstemp() failed [%s]", filename, strerror(errno));
        astra_abort();
    }

    static char pid[8];
    int size = snprintf(pid, sizeof(pid), "%d\n", getpid());
    if(write(fd, pid, size) == -1)
    {
        fprintf(stderr, "[pidfile %s] write() failed [%s]\n", filename, strerror(errno));
        astra_abort();
    }

    fchmod(fd, 0644);
    close(fd);

    const int link_ret = link(tmp_pidfile, filename);
    unlink(tmp_pidfile);
    if(link_ret == -1)
    {
        asc_log_error("[pidfile %s] link() failed [%s]", filename, strerror(errno));
        astra_abort();
    }

    // store in registry to prevent the instance destroying
    lua_pushvalue(lua, 3);
    mod->idx_self = luaL_ref(lua, LUA_REGISTRYINDEX);
}

static void module_destroy(module_data_t *mod)
{
    asc_log_info("[pidfile %s] %s()", filename, __FUNCTION__);

    __uarg(mod);

    if(!access(filename, W_OK))
        unlink(filename);

    filename = NULL;

    luaL_unref(lua, LUA_REGISTRYINDEX, mod->idx_self);
}

MODULE_LUA_METHODS()
{
    { NULL, NULL }
};
MODULE_LUA_REGISTER(pidfile)

#endif
