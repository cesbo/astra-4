/*
 * Astra Core
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2015, Andrey Dyldin <and@cesbo.com>
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

#ifndef _ASC_THREAD_H_
#define _ASC_THREAD_H_ 1

#include "base.h"

typedef struct asc_thread_t asc_thread_t;
typedef struct asc_thread_buffer_t asc_thread_buffer_t;
typedef void (*thread_callback_t)(void *);

void asc_thread_core_init(void);
void asc_thread_core_destroy(void);
void asc_thread_core_loop(void);

asc_thread_t * asc_thread_init(void *arg) __wur;
void asc_thread_start(  asc_thread_t *thread
                      , thread_callback_t loop
                      , thread_callback_t on_read, asc_thread_buffer_t *buffer
                      , thread_callback_t on_close);
void asc_thread_destroy(asc_thread_t *thread);

asc_thread_buffer_t * asc_thread_buffer_init(size_t buffer_size) __wur;
void asc_thread_buffer_destroy(asc_thread_buffer_t *buffer);

void asc_thread_buffer_flush(asc_thread_buffer_t *buffer);

ssize_t asc_thread_buffer_read(asc_thread_buffer_t *buffer, void *data, size_t size) __wur;
ssize_t asc_thread_buffer_write(asc_thread_buffer_t *buffer, const void *data, size_t size) __wur;

#endif /* _ASC_THREAD_H_ */
