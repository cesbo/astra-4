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
    int _empty;
};

static char *filename = NULL;

/* required */

static void module_init(module_data_t *mod)
{
    __uarg(mod);

    const int idx_value = 2;
    const char *value = luaL_checkstring(lua, idx_value);
    if(filename)
    {
        asc_log_error("[pidfile %s] already created in %s", value, filename);
        astra_abort();
    }

    filename = malloc(luaL_len(lua, idx_value) + 1);
    strcpy(filename, value);

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
}

static void module_destroy(module_data_t *mod)
{
    __uarg(mod);

    if(!access(filename, W_OK))
        unlink(filename);

    free(filename);
    filename = NULL;
}

MODULE_LUA_METHODS()
{
    { NULL, NULL }
};
MODULE_LUA_REGISTER(pidfile)

#endif
