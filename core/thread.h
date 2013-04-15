/*
 * Astra Core
 * http://cesbo.com
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#ifndef _THREAD_H_
#define _THREAD_H_ 1

#include "base.h"

typedef struct asc_thread_t asc_thread_t;

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
