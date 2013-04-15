/*
 * Astra Core
 * http://cesbo.com
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#ifndef _LOG_H_
#define _LOG_H_ 1

#include "base.h"

void asc_log_set_stdout(int);
void asc_log_set_debug(int);
void asc_log_set_file(const char *);
#ifndef _WIN32
void asc_log_set_syslog(const char *);
#endif

void asc_log_hup(void);
void asc_log_core_destroy(void);

void asc_log_info(const char *, ...);
void asc_log_error(const char *, ...);
void asc_log_warning(const char *, ...);
void asc_log_debug(const char *, ...);

int asc_log_is_debug(void);

#endif /* _LOG_H_ */
