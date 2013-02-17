/*
 * Astra Core
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#include "asc.h"

struct timer_s
{
    void (*callback)(void *arg);
    void *arg;

    struct timeval interval;
    struct timeval next_shot;
};

static list_t *timer_list = NULL;

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

void timer_core_init(void)
{
    timer_list = list_init();
}

void timer_core_destroy(void)
{
    list_first(timer_list);
    while(list_is_data(timer_list))
    {
        free(list_data(timer_list));
        list_remove_current(timer_list);
    }
}

void timer_core_loop(void)
{
    int is_detached = 0;
    struct timeval cur;

    list_for(timer_list)
    {
        timer_t *timer = (timer_t *)list_data(timer_list);
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

    list_first(timer_list);
    while(list_is_data(timer_list))
    {
        timer_t *timer = (timer_t *)list_data(timer_list);
        if(timer->callback)
            list_next(timer_list);
        else
        {
            free(list_data(timer_list));
            list_remove_current(timer_list);
        }
    }
}

timer_t * timer_attach(unsigned int ms, void (*callback)(void *), void *arg)
{
    timer_t *timer = (timer_t *)calloc(1, sizeof(timer_t));
    timer->interval.tv_sec = ms / 1000;
    timer->interval.tv_usec = (ms % 1000) * 1000;
    timer->callback = callback;
    timer->arg = arg;

    struct timeval cur;
    gettimeofday(&cur, NULL);
    timeradd(&cur, &timer->interval, &timer->next_shot);

    list_insert_tail(timer_list, timer);

    return timer;
}

void timer_one_shot(unsigned int ms, void (*callback)(void *), void *arg)
{
    timer_t *timer = timer_attach(ms, callback, arg);
    timer->interval.tv_sec = 0;
    timer->interval.tv_usec = 0;
}

void timer_detach(timer_t *timer)
{
    if(!timer)
        return;

    timer->callback = NULL;
}
