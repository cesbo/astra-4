/*
 * Astra Core
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

#define ASC
#include "log.h"

#ifndef _WIN32
#include <syslog.h>
#endif
#include <stdarg.h>

typedef struct
{
    int fd;
    bool color;
    bool debug;
    bool sout;
    char *filename;
#ifndef _WIN32
    char *syslog;
#endif
} log_t;

static log_t __log =
{
    0,
    false,
    false,
    true,
    NULL,
#ifndef _WIN32
    NULL,
#endif
};

enum
{
    LOG_TYPE_INFO       = 0x00000001,
    LOG_TYPE_ERROR      = 0x00000002,
    LOG_TYPE_WARNING    = 0x00000004,
    LOG_TYPE_DEBUG      = 0x00000008
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
    char buffer[4096];

    size_t len_1 = 0; // to skip time stamp
    time_t ct = time(NULL);
    struct tm *sct = localtime(&ct);
    len_1 = strftime(buffer, sizeof(buffer), "%b %d %X: ", sct);

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

    if(__log.sout)
    {
        bool reset_color = false;
        if(__log.color && isatty(STDOUT_FILENO))
        {
            switch(type)
            {
                case LOG_TYPE_WARNING:
                    if(write(STDOUT_FILENO, "\x1b[33m", 5) != -1)
                        reset_color = true;
                    break;
                case LOG_TYPE_ERROR:
                    if(write(STDOUT_FILENO, "\x1b[31m", 5) != -1)
                        reset_color = true;
                    break;
                default:
                    break;
            }
        }
        const int r = write(1, buffer, len_2);
        if(reset_color && write(STDOUT_FILENO, "\x1b[0m", 4) != -1) {};
        if(r == -1)
            fprintf(stderr, "[log] failed to write to the stdout [%s]\n", strerror(errno));
    }

    if(__log.fd && write(__log.fd, buffer, len_2) == -1)
        fprintf(stderr, "[log] failed to write to the file [%s]\n", strerror(errno));
}

void asc_log_info(const char *msg, ...)
{
    va_list ap;
    va_start(ap, msg);
    _log(LOG_TYPE_INFO, msg, ap);
    va_end(ap);
}

void asc_log_error(const char *msg, ...)
{
    va_list ap;
    va_start(ap, msg);
    _log(LOG_TYPE_ERROR, msg, ap);
    va_end(ap);
}

void asc_log_warning(const char *msg, ...)
{
    va_list ap;
    va_start(ap, msg);
    _log(LOG_TYPE_WARNING, msg, ap);
    va_end(ap);
}

void asc_log_debug(const char *msg, ...)
{
    if(!__log.debug)
        return;

    va_list ap;
    va_start(ap, msg);
    _log(LOG_TYPE_DEBUG, msg, ap);
    va_end(ap);
}

bool asc_log_is_debug(void)
{
    return __log.debug;
}

void asc_log_hup(void)
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

    if(__log.fd == -1)
    {
        __log.fd = 0;
        __log.sout = true;
        asc_log_error("[core/log] failed to open %s (%s)", __log.filename, strerror(errno));
    }
}

void asc_log_core_destroy(void)
{
    if(__log.fd != 1)
    {
        close(__log.fd);
        __log.fd = 0;
    }

#ifndef _WIN32
    if(__log.syslog)
    {
        closelog();
        free(__log.syslog);
        __log.syslog = NULL;
    }
#endif

    __log.color = false;
    __log.debug = false;
    __log.sout = true;
    if(__log.filename)
    {
        free(__log.filename);
        __log.filename = NULL;
    }
}

void asc_log_set_stdout(bool val)
{
    __log.sout = val;
}

void asc_log_set_debug(bool val)
{
    __log.debug = val;
}

void asc_log_set_color(bool val)
{
    __log.color = val;
}

void asc_log_set_file(const char *val)
{
    if(__log.filename)
    {
        free(__log.filename);
        __log.filename = NULL;
    }

    if(val)
        __log.filename = strdup(val);

    asc_log_hup();
}

#ifndef _WIN32
void asc_log_set_syslog(const char *val)
{
    if(__log.syslog)
    {
        closelog();
        free(__log.syslog);
        __log.syslog = NULL;
    }

    if(!val)
        return;

    __log.syslog = strdup(val);
    openlog(__log.syslog, LOG_PID | LOG_CONS, LOG_USER);
}
#endif
