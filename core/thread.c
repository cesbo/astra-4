/*
 * Astra Core
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#define ASC
#include "asc.h"

#ifdef _WIN32
#   include <windows.h>
#else
#   include <signal.h>
#   include <pthread.h>
#endif

static jmp_buf global_jmp;

struct thread_s
{
    jmp_buf jmp;
    int is_set_jmp;

    void (*loop)(void *);
    void *arg;

#ifdef _WIN32
    HANDLE thread;
#else
    pthread_t thread;
#endif
};

#ifdef _WIN32

DWORD WINAPI thread_loop(void *arg)
{
    thread_t *thread = arg;
    thread->loop(thread->arg);
    return 0;
}

#else

static void thread_handler(int sig)
{
    if(sig == SIGUSR1)
        longjmp(global_jmp, 1);
}

inline jmp_buf * __thread_getjmp(void)
{
    return &global_jmp;
}

void __thread_setjmp(thread_t *thread)
{
    memcpy(&thread->jmp, &global_jmp, sizeof(jmp_buf));
    thread->is_set_jmp = 1;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = thread_handler;
    if(sigaction(SIGUSR1, &sa, NULL) < 0)
    {
        log_error("[core/thread] sigaction() failed\n");
        abort();
    }
}

static void * thread_loop(void *arg)
{
    thread_t *thread = arg;
    thread->loop(thread->arg);
    return NULL;
}

#endif /* ! _WIN32 */

void thread_init(thread_t **thread_ptr, void (*loop)(void *), void *arg)
{
    thread_t *thread = calloc(1, sizeof(thread_t));
    thread->loop = loop;
    thread->arg = arg;
    *thread_ptr = thread;

#ifdef _WIN32
    DWORD tid;
    thread->thread = CreateThread(NULL, 0, &thread_loop, thread, 0, &tid);
    if(thread->thread != NULL)
        return;
#else
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    const int ret = pthread_create(&thread->thread, &attr, thread_loop, thread);
    pthread_attr_destroy(&attr);
    if(ret == 0)
        return;
#endif

    *thread_ptr = NULL;
    free(thread);
    log_error("[core/thread] failed to start thread");
    abort();
}

void thread_destroy(thread_t **thread_ptr)
{
    if(!thread_ptr)
        return;
    thread_t *thread = *thread_ptr;
    if(!thread)
        return;

#ifdef _WIN32
    TerminateThread(thread->thread, 0);
    CloseHandle(thread->thread);
#else
    if(thread->is_set_jmp)
    {
        memcpy(&global_jmp, &thread->jmp, sizeof(jmp_buf));
        pthread_kill(thread->thread, SIGUSR1);
    }
    pthread_join(thread->thread, NULL);
#endif

    free(thread);
    *thread_ptr = NULL;
}
