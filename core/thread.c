/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#define ASTRA_CORE
#include <astra.h>

#ifdef _WIN32
#   include <windows.h>
#else
#   include <signal.h>
#   include <pthread.h>
#endif

typedef enum
{
    THREAD_UNKNOWN = 0,
    THREAD_STARTED = 1,
    THREAD_FINISHED = 2,
} thread_status_t;

struct thread_s
{
    thread_status_t status; // must be first (for IS_THREAD_STARTED)

    void (*loop)(void *);
    void *arg;

#ifdef _WIN32
    HANDLE thread;
#else
    pthread_t thread;
#endif
};

#ifndef _WIN32
static void ignore_handler(int sig)
{}
#endif

#ifdef _WIN32
DWORD WINAPI thread_loop(void *arg)
#else
static void * thread_loop(void *arg)
#endif
{
    thread_t *t = arg;

#ifndef _WIN32
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = ignore_handler;
    if(sigaction(SIGUSR1, &sa, NULL) < 0)
        pthread_exit(NULL);
#endif

    t->status = THREAD_STARTED;
    t->loop(t->arg);
    t->status = THREAD_FINISHED;

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

int thread_init(thread_t **tptr, void (*loop)(void *), void *arg)
{
    if(!tptr || !loop)
        return 0;

    thread_t *t = calloc(1, sizeof(thread_t));
    t->loop = loop;
    t->arg = arg;
    *tptr = t;

    int ret = 0;
#ifdef _WIN32
    DWORD tid;
    t->thread = CreateThread(NULL, 0, &thread_loop, thread, 0, &tid);
    if(!t->thread)
        ret = -1;
#else
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    ret = pthread_create(&t->thread, &attr, thread_loop, t);
    pthread_attr_destroy(&attr);
#endif

    if(ret != 0)
    {
        *tptr = NULL;
        free(t);
        return 0;
    }

    return 1;
}

void thread_destroy(thread_t **tptr)
{
    if(!tptr)
        return;
    thread_t *t = *tptr;
    if(!t)
        return;

    t->status = THREAD_FINISHED;

#ifdef _WIN32
    TerminateThread(t->thread, 0);
    CloseHandle(t->thread);
#else
    pthread_kill(t->thread, SIGUSR1);
    pthread_join(t->thread, NULL);
#endif

    free(t);
    *tptr = NULL;
}

inline int thread_is_started(thread_t *t)
{
    return (t && t->status == THREAD_STARTED);
}
