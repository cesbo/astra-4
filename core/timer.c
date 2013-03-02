/*
 * Astra Core
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#include "asc.h"

struct asc_timer_t
{
    void (*callback)(void *arg);
    void *arg;

    struct timeval interval;
    struct timeval next_shot;
};

static asc_list_t *timer_list = NULL;

#ifndef timeradd
#define timeradd(tvp, uvp, vvp)                                         \
    do {                                                                \
            (vvp)->tv_sec = (tvp)->tv_sec + (uvp)->tv_sec;              \
            (vvp)->tv_usec = (tvp)->tv_usec + (uvp)->tv_usec;           \
            if ((vvp)->tv_usec >= 1000000) {                            \
                    (vvp)->tv_sec++;                                    \
                    (vvp)->tv_usec -= 1000000;                          \
            }                                                           \
    } while (0)
#endif

void asc_timer_core_init(void)
{
    timer_list = asc_list_init();
}

void asc_timer_core_destroy(void)
{
    asc_list_first(timer_list);
    while(asc_list_eol(timer_list))
    {
        free(asc_list_data(timer_list));
        asc_list_remove_current(timer_list);
    }
}

void asc_timer_core_loop(void)
{
    int is_detached = 0;
    struct timeval cur;

    asc_list_for(timer_list)
    {
        asc_timer_t *timer = asc_list_data(timer_list);
        if(!timer->callback)
        {
            ++is_detached;
            continue;
        }

        gettimeofday(&cur, NULL);
        if(timercmp(&cur, &timer->next_shot, >=))
        {
            if(timer->interval.tv_sec == 0 && timer->interval.tv_usec == 0)
            {
                // one shot timer
                timer->callback(timer->arg);
                timer->callback = NULL;
                ++is_detached;
            }
            else
            {
                timeradd(&cur, &timer->interval, &timer->next_shot);
                timer->callback(timer->arg);
            }
        }
    }

    if(!is_detached)
        return;

    asc_list_first(timer_list);
    while(asc_list_eol(timer_list))
    {
        asc_timer_t *timer = asc_list_data(timer_list);
        if(timer->callback)
            asc_list_next(timer_list);
        else
        {
            free(asc_list_data(timer_list));
            asc_list_remove_current(timer_list);
        }
    }
}

asc_timer_t * asc_timer_init(unsigned int ms, void (*callback)(void *), void *arg)
{
    asc_timer_t *timer = calloc(1, sizeof(asc_timer_t));
    timer->interval.tv_sec = ms / 1000;
    timer->interval.tv_usec = (ms % 1000) * 1000;
    timer->callback = callback;
    timer->arg = arg;

    struct timeval cur;
    gettimeofday(&cur, NULL);
    timeradd(&cur, &timer->interval, &timer->next_shot);

    asc_list_insert_tail(timer_list, timer);

    return timer;
}

void timer_one_shot(unsigned int ms, void (*callback)(void *), void *arg)
{
    asc_timer_t *timer = asc_timer_init(ms, callback, arg);
    timer->interval.tv_sec = 0;
    timer->interval.tv_usec = 0;
}

void asc_timer_destroy(asc_timer_t *timer)
{
    if(!timer)
        return;

    timer->callback = NULL;
}
