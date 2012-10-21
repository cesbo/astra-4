/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#ifdef _WIN32
#   error "pidfile module is not for win32"
#else

#include <astra.h>
#include <sys/stat.h>

#define LOG_MSG(_msg) "[pidfile %s] " _msg, mod->filename

struct module_data_s
{
    MODULE_BASE();

    const char *filename;
};

static const char __registry_field[] = "__astra_pidfile";
static const char __filename_field[] = "filename";
static const char __self_field[] = "self";

/* required */

static void module_init(module_data_t *mod)
{
    lua_State *L = LUA_STATE(mod);
    const int value = 2;
    const int self = lua_gettop(L);

    mod->filename = luaL_checkstring(L, value);

    lua_getfield(L, LUA_REGISTRYINDEX, __registry_field);
    if(!lua_isnil(L, -1))
    {
        lua_getfield(L, -1, __filename_field);
        const char *pfn = luaL_checkstring(L, -1);
        log_warning(LOG_MSG("already created in %s"), pfn);
        lua_pop(L, 2); // filename + registry
        return;
    }
    lua_pop(L, 1); // registry

    if(!access(mod->filename, W_OK))
        unlink(mod->filename);

    char tmp_pidfile[256];
    snprintf(tmp_pidfile, sizeof(tmp_pidfile), "%s.XXXXXX", mod->filename);
    int fd = mkstemp(tmp_pidfile);
    if(fd == -1)
    {
        log_error(LOG_MSG("failed to create temporary file (%s)")
                  , strerror(errno));
        return;
    }

    char pid[8];
    int size = snprintf(pid, sizeof(pid), "%d\n", getpid());
    if(write(fd, pid, size) == -1)
        ;
    fchmod(fd, 0644);
    close(fd);

    const int link_ret = link(tmp_pidfile, mod->filename);
    unlink(tmp_pidfile);
    if(link_ret == -1)
    {
        log_error(LOG_MSG("filed to create pidfile (%s)"), strerror(errno));
        return;
    }

    // store in registry to prevent the instance destroying
    lua_newtable(L);
    lua_pushvalue(L, self);
    lua_setfield(L, -2, __self_field);
    lua_pushvalue(L, value);
    lua_setfield(L, -2, __filename_field);
    lua_setfield(L, LUA_REGISTRYINDEX, __registry_field);
}

static void module_destroy(module_data_t *mod)
{
    if(!access(mod->filename, W_OK))
        unlink(mod->filename);
}

MODULE_OPTIONS_EMPTY();
MODULE_METHODS_EMPTY();

MODULE(pidfile)

#endif
