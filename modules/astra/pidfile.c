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

struct module_data_s
{
    int _empty;
};

static char *filename = NULL;

/* required */

static void module_init(module_data_t *mod)
{
    const int idx_value = 2;
    const char *value = luaL_checkstring(lua, idx_value);
    if(filename)
    {
        log_error("[pidfile %s] already created in %s", value, filename);
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
        log_error("[pidfile %s] failed to create temporary file [%s]", filename, strerror(errno));
        free(filename);
        astra_abort();
    }

    static char pid[8];
    int size = snprintf(pid, sizeof(pid), "%d\n", getpid());
    if(write(fd, pid, size) == -1)
        ;
    fchmod(fd, 0644);
    close(fd);

    const int link_ret = link(tmp_pidfile, filename);
    unlink(tmp_pidfile);
    if(link_ret == -1)
    {
        log_error("[pidfile %s] filed to create pidfile [%s]", filename, strerror(errno));
        free(filename);
        astra_abort();
    }
}

static void module_destroy(module_data_t *mod)
{
    if(!access(filename, W_OK))
        unlink(filename);

    free(filename);
    filename = NULL;
}

MODULE_METHODS_EMPTY();

MODULE_LUA_REGISTER(pidfile)

#endif
