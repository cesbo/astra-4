/*
 * AsC Framework
 * http://cesbo.com
 *
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#define ASC
#include "asc.h"

// TODO: pre-buffered nodes

struct list_s
{
    void *data;
    list_t *prev;
    list_t *next;
};

list_t * list_append(list_t *li, void *data)
{
    list_t *nli = (list_t *)malloc(sizeof(list_t));
    nli->data = data;
    if(!li)
    {
        nli->prev = nli->next = NULL;
    }
    else
    {
        nli->prev = li;
        nli->next = li->next;
        if(li->next)
            li->next->prev = nli;
        li->next = nli;
    }
    return nli;
}

list_t * list_insert(list_t *li, void *data)
{
    list_t *nli = (list_t *)malloc(sizeof(list_t));
    nli->data = data;
    if(!li)
    {
        nli->prev = nli->next = NULL;
    }
    else
    {
        nli->next = li;
        nli->prev = li->prev;
        if(li->prev)
            li->prev->next = nli;
        li->prev = nli;
    }
    return nli;
}

list_t * list_delete(list_t *li, void *data)
{
    if(!li)
        return NULL;

    list_t *i = li;
    if(data)
    {
        i = list_get_first(li);
        while(i)
        {
            if(data == list_get_data(i))
                break;
            i = i->next;
        }
        if(!i)
            return li;
    }

    list_t *r = NULL;
    if(i->next)
    {
        i->next->prev = i->prev;
        r = i->next;
    }
    if(i->prev)
    {
        i->prev->next = i->next;
        r = i->prev;
    }
    free(i);
    return r;
}

inline list_t * list_get_first(list_t *li)
{
    if(!li)
        return NULL;
    while(li->prev)
        li = li->prev;
    return li;
}

inline list_t * list_get_next(list_t *li)
{
    return li->next;
}

inline void * list_get_data(list_t *li)
{
    return li->data;
}
