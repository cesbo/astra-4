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

#ifndef _THREAD_H_
#define _THREAD_H_ 1

#include "base.h"

typedef struct asc_thread_t asc_thread_t;

void asc_thread_core_init(void);
void asc_thread_core_destroy(void);
void asc_thread_core_loop(void);

#ifndef _WIN32
jmp_buf * __thread_getjmp(void);
void __thread_setjmp(asc_thread_t *thread);
#   define asc_thread_while(_thread)                                                            \
        const int __thread_loop = setjmp(*__thread_getjmp());                                   \
        if(!__thread_loop) __thread_setjmp(_thread);                                            \
        while(!__thread_loop)
#else
#   define thread_while() while(1)
#endif

void asc_thread_init(asc_thread_t **thread_ptr, void (*loop)(void *), void *arg);
void asc_thread_destroy(asc_thread_t **thread_ptr);

#endif /* _THREAD_H_ */
