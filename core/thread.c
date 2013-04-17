/*
 * Astra Core
 * http://cesbo.com
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#include "assert.h"
#include "thread.h"
#include "log.h"

#ifdef _WIN32
#   include <windows.h>
#else
#   include <signal.h>
#   include <pthread.h>
#endif

static jmp_buf global_jmp;

struct asc_thread_t
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
    asc_thread_t *thread = arg;
    thread->loop(thread->arg);
    thread->loop = NULL;
    return 0;
}

#else

static void thread_handler(int sig)
{
    if(sig == SIGUSR1)
        longjmp(global_jmp, 1);
}

jmp_buf * __thread_getjmp(void)
{
    return &global_jmp;
}

void __thread_setjmp(asc_thread_t *thread)
{
    memcpy(&thread->jmp, &global_jmp, sizeof(jmp_buf));
    thread->is_set_jmp = 1;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = thread_handler;
    const int r = sigaction(SIGUSR1, &sa, NULL);
    asc_assert(r == 0, "[core/thread] sigaction() failed");
}

static void * thread_loop(void *arg)
{
    asc_thread_t *thread = arg;
    thread->loop(thread->arg);
    thread->loop = NULL;
    pthread_exit(NULL);
}

#endif /* ! _WIN32 */

void asc_thread_init(asc_thread_t **thread_ptr, void (*loop)(void *), void *arg)
{
    asc_thread_t *thread = calloc(1, sizeof(asc_thread_t));
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
    asc_assert(0, "[core/thread] failed to start thread");
}

void asc_thread_destroy(asc_thread_t **thread_ptr)
{
    if(!thread_ptr)
        return;
    asc_thread_t *thread = *thread_ptr;
    if(!thread)
        return;

    if(thread->loop)
    {
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
        thread->loop = NULL;
    }

    free(thread);
    *thread_ptr = NULL;
}
