/*
 * AsC Framework
 * http://cesbo.com
 *
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#define ASC
#include "asc.h"

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

typedef struct
{
    void (*cb)(void *);
    void *arg;

    int is_one_shot;

    struct timeval interval;
    struct timeval next_shot;
} timer_item_t;

void timer_destroy(void)
{
    list_t *i = list_get_first(timer_list);
    timer_list = NULL;
    while(i)
    {
        free(list_get_data(i));
        i = list_delete(i, NULL);
    }
}

static timer_item_t * _timer_attach(unsigned int msec
                                    , void (*cb)(void *), void *arg
                                    , int is_one_shot)
{
    timer_item_t *item = (timer_item_t *)malloc(sizeof(timer_item_t));
    item->interval.tv_sec = msec / 1000;
    item->interval.tv_usec = (msec % 1000) * 1000;
    item->cb = cb;
    item->arg = arg;
    item->is_one_shot = is_one_shot;

    struct timeval cur;
    gettimeofday(&cur, NULL);
    timeradd(&cur, &item->interval, &item->next_shot);
    timer_list = list_insert(timer_list, item);

    return item;
}

void * timer_attach(unsigned int msec, void (*cb)(void *), void *arg)
{
    return _timer_attach(msec, cb, arg, 0);
}

void timer_one_shot(unsigned int msec, void (*cb)(void *), void *arg)
{
    _timer_attach(msec, cb, arg, 1);
}

void timer_detach(void *id)
{
    if(!id)
        return;
    timer_item_t *item = (timer_item_t *)id;
    item->cb = NULL; // delete item on next action
}

void timer_action(void)
{
    list_t *i = list_get_first(timer_list);
    struct timeval cur;
    while(i)
    {
        timer_item_t *item = (timer_item_t *)list_get_data(i);
        list_t *next_i = list_get_next(i);

        if(!item->cb)
        {
            // detach item
            free(item);

            if(i == timer_list)
                timer_list = list_delete(i, NULL);
            else
                list_delete(i, NULL);

            if(!next_i) // last item
                break;
            i = next_i;
            continue;
        }

        gettimeofday(&cur, NULL);
        if(timercmp(&cur, &item->next_shot, >=))
        {
            timeradd(&cur, &item->interval, &item->next_shot);
            item->cb(item->arg);
            if(item->is_one_shot)
                timer_detach(item);
        }

        i = next_i;
    }
}
