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
