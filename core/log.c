/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#define ASTRA_CORE
#include <astra.h>

#include <fcntl.h>
#ifndef _WIN32
#include <syslog.h>
#endif
#include <stdarg.h>

static struct
{
    int debug;
#ifndef _WIN32
    int syslog;
#endif
    int fd;
    int sout;
    const char *filename;
} log_g =
{
    .debug = 0
#ifndef _WIN32
    , .syslog = 0
#endif
    , .fd = 0
    , .sout = 1
    , .filename = NULL
};

enum
{
    LOG_TYPE_INFO           = 0x00000001
    , LOG_TYPE_ERROR        = 0x00000002
    , LOG_TYPE_WARNING      = 0x00000004
    , LOG_TYPE_DEBUG        = 0x00000008
};

#ifndef _WIN32
static int _get_type_syslog(int type)
{
    switch(type & 0x000000FF)
    {
        case LOG_TYPE_INFO: return LOG_INFO;
        case LOG_TYPE_WARNING: return LOG_WARNING;
        case LOG_TYPE_DEBUG: return LOG_DEBUG;
        case LOG_TYPE_ERROR:
        default: return LOG_ERR;
    }
}
#endif

static const char * _get_type_str(int type)
{
    switch(type & 0x000000FF)
    {
        case LOG_TYPE_INFO: return "INFO";
        case LOG_TYPE_WARNING: return "WARNING";
        case LOG_TYPE_DEBUG: return "DEBUG";
        case LOG_TYPE_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

static void _log(int type, const char *msg, va_list ap)
{
    static char buffer[4096];

    if(type == LOG_TYPE_DEBUG && !log_g.debug)
        return;

    size_t len_1 = 0; // to skip time stamp
    if(log_g.fd || log_g.sout)
    {
        time_t ct = time(NULL);
        struct tm *sct = localtime(&ct);
        len_1 = strftime(buffer, sizeof(buffer), "%b %d %X: ", sct);
    }

    size_t len_2 = len_1;
    const char *type_str = _get_type_str(type);
    len_2 += snprintf(&buffer[len_2], sizeof(buffer) - len_2, "%s: ", type_str);
    if(ap)
        len_2 += vsnprintf(&buffer[len_2], sizeof(buffer) - len_2, msg, ap);
    else
        len_2 += snprintf(&buffer[len_2], sizeof(buffer) - len_2, "%s", msg);

#ifndef _WIN32
    if(log_g.syslog)
        syslog(_get_type_syslog(type), "%s", &buffer[len_1]);
#endif

    buffer[len_2] = '\n';
    ++len_2;

    if(log_g.sout && write(1, buffer, len_2) == -1)
        ;

    if(log_g.fd && write(log_g.fd, buffer, len_2) == -1)
        ;
}

inline void log_info(const char *msg, ...)
{
    va_list ap;
    va_start(ap, msg);
    _log(LOG_TYPE_INFO, msg, ap);
    va_end(ap);
}

inline void log_error(const char *msg, ...)
{
    va_list ap;
    va_start(ap, msg);
    _log(LOG_TYPE_ERROR, msg, ap);
    va_end(ap);
}

inline void log_warning(const char *msg, ...)
{
    va_list ap;
    va_start(ap, msg);
    _log(LOG_TYPE_WARNING, msg, ap);
    va_end(ap);
}

inline void log_debug(const char *msg, ...)
{
    va_list ap;
    va_start(ap, msg);
    _log(LOG_TYPE_DEBUG, msg, ap);
    va_end(ap);
}

/* API */

void log_hup(void)
{
    if(log_g.fd > 1)
        close(log_g.fd);

    if(!log_g.filename)
        return;

    log_g.fd = open(log_g.filename, O_WRONLY | O_CREAT | O_APPEND
#ifndef _WIN32
                    , S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
#else
                    , S_IRUSR | S_IWUSR);
#endif

    if(log_g.fd <= 0)
    {
        log_g.fd = 0;
        log_g.sout = 1;
        log_error("[core/log] failed to open %s (%s)"
                  , log_g.filename, strerror(errno));
    }
}

void log_destroy(void)
{
    if(log_g.fd > 1)
        close(log_g.fd);
    log_g.fd = 0;

#ifndef _WIN32
    if(log_g.syslog > 0)
        closelog();
    log_g.syslog = 0;
#endif

    log_g.debug = 0;
    log_g.sout = 1;
    log_g.filename = NULL;
}

#ifdef WITH_LUA

static int lua_log_set(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);

    // store in registry to prevent the gc cleaning
    lua_pushstring(L, "astra.log");
    lua_pushvalue(L, 1);
    lua_settable(L, LUA_REGISTRYINDEX);

    for(lua_pushnil(L); lua_next(L, 1); lua_pop(L, 1))
    {
        const char *var = lua_tostring(L, -2);

        if(!strcmp(var, "debug"))
        {
            luaL_checktype(L, -1, LUA_TBOOLEAN);
            log_g.debug = lua_toboolean(L, -1);
        }
        else if(!strcmp(var, "filename"))
        {
            luaL_checktype(L, -1, LUA_TSTRING);
            log_g.filename = lua_tostring(L, -1);
            log_hup();
        }
        else if(!strcmp(var, "syslog"))
        {
#ifndef _WIN32
            luaL_checktype(L, -1, LUA_TSTRING);
            const char *syslog_name = lua_tostring(L, -1);
            openlog(syslog_name, LOG_PID | LOG_CONS, LOG_USER);
            log_g.syslog = 1;
#endif
        }
        else if(!strcmp(var, "stdout"))
        {
            luaL_checktype(L, -1, LUA_TBOOLEAN);
            log_g.sout = lua_toboolean(L, -1);
        }
    }

    return 0;
}

static int lua_log_error(lua_State *L)
{
    const char *str = luaL_checkstring(L, 1);
    _log(LOG_TYPE_ERROR, str, NULL);
    return 0;
}

static int lua_log_warning(lua_State *L)
{
    const char *str = luaL_checkstring(L, 1);
    _log(LOG_TYPE_WARNING, str, NULL);
    return 0;
}

static int lua_log_info(lua_State *L)
{
    const char *str = luaL_checkstring(L, 1);
    _log(LOG_TYPE_INFO, str, NULL);
    return 0;
}

static int lua_log_debug(lua_State *L)
{
    const char *str = luaL_checkstring(L, 1);
    _log(LOG_TYPE_DEBUG, str, NULL);
    return 0;
}

void log_init(lua_State *L)
{
    static const luaL_Reg api[] =
    {
        { "set", lua_log_set },
        { "error", lua_log_error },
        { "warning", lua_log_warning },
        { "info", lua_log_info },
        { "debug", lua_log_debug },
        { NULL, NULL }
    };

    luaL_newlib(L, api);
    lua_setglobal(L, "log");
}

#endif /* WITH_LUA */
