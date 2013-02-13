/*
 * Astra Core
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#define ASC
#include "asc.h"

#include <fcntl.h>
#ifndef _WIN32
#include <syslog.h>
#endif
#include <stdarg.h>

typedef struct
{
    int fd;
    int debug;
    int sout;
    char *filename;
#ifndef _WIN32
    int syslog;
#endif
} log_t;

static log_t __log =
{
    0,
    0,
    1,
    NULL
#ifndef _WIN32
    , 0
#endif
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

    size_t len_1 = 0; // to skip time stamp
    if(__log.fd || __log.sout)
    {
        time_t ct = time(NULL);
        struct tm *sct = localtime(&ct);
        len_1 = strftime(buffer, sizeof(buffer), "%b %d %X: ", sct);
    }

    size_t len_2 = len_1;
    const char *type_str = _get_type_str(type);
    len_2 += snprintf(&buffer[len_2], sizeof(buffer) - len_2, "%s: ", type_str);
    len_2 += vsnprintf(&buffer[len_2], sizeof(buffer) - len_2, msg, ap);

#ifndef _WIN32
    if(__log.syslog)
        syslog(_get_type_syslog(type), "%s", &buffer[len_1]);
#endif

    buffer[len_2] = '\n';
    ++len_2;

    if(__log.sout && write(1, buffer, len_2) == -1)
        ;

    if(__log.fd && write(__log.fd, buffer, len_2) == -1)
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
    if(!__log.debug)
        return;

    va_list ap;
    va_start(ap, msg);
    _log(LOG_TYPE_DEBUG, msg, ap);
    va_end(ap);
}

/* API */

void log_hup(void)
{
    if(__log.fd > 1)
    {
        close(__log.fd);
        __log.fd = 0;
    }

    if(!__log.filename)
        return;

    __log.fd = open(__log.filename, O_WRONLY | O_CREAT | O_APPEND
#ifndef _WIN32
                    , S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
#else
                    , S_IRUSR | S_IWUSR);
#endif

    if(__log.fd <= 0)
    {
        __log.fd = 0;
        __log.sout = 1;
        log_error("[core/log] failed to open %s (%s)"
                  , __log.filename, strerror(errno));
    }
}

void log_destroy(void)
{
    if(__log.fd > 1)
        close(__log.fd);
    __log.fd = 0;

#ifndef _WIN32
    if(__log.syslog > 0)
        closelog();
    __log.syslog = 0;
#endif

    __log.debug = 0;
    __log.sout = 1;
    if(__log.filename)
    {
        free(__log.filename);
        __log.filename = NULL;
    }
}

void log_set_stdout(int val)
{
    __log.sout = val;
}

void log_set_debug(int val)
{
    __log.debug = val;
}

void log_set_file(const char *val)
{
    if(__log.filename)
    {
        free(__log.filename);
        __log.filename = NULL;
    }

    if(val)
        __log.filename = strdup(val);

    log_hup();
}

#ifndef _WIN32
void log_set_syslog(const char *val)
{
    if(__log.syslog)
    {
        closelog();
        __log.syslog = 0;
    }

    if(!val)
        return;

    openlog(val, LOG_PID | LOG_CONS, LOG_USER);
    __log.syslog = 1;
}
#endif
