/*
 * Astra Core
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#define ASC
#include "asc.h"

#include <sys/queue.h>

#define MSG() "[core/list] %s():%d", __FUNCTION__, __LINE__

typedef struct item_s
{
    void *data;
    TAILQ_ENTRY(item_s) entries;
} item_t;

struct list_s
{
    struct item_s *current;
    TAILQ_HEAD(list_head_s, item_s) list;
};

list_t * list_init(void)
{
    list_t *list = (list_t *)malloc(sizeof(list_t));
    TAILQ_INIT(&list->list);
    list->current = NULL;
    return list;
}

void list_destroy(list_t *list)
{
    if(list->current)
    {
        log_error(MSG());
        abort();
    }
    free(list);
}

inline void list_first(list_t *list)
{
    list->current = TAILQ_FIRST(&list->list);
}

inline void list_next(list_t *list)
{
    if(list->current)
        list->current = TAILQ_NEXT(list->current, entries);
}

inline int list_is_data(list_t *list)
{
    return (list->current != NULL);
}

inline void * list_data(list_t *list)
{
    return list->current;
}

void list_insert_head(list_t *list, void *data)
{
    item_t *item = (item_t *)malloc(sizeof(item_t));
    item->data = data;
    item->entries.tqe_next = NULL;
    item->entries.tqe_prev = NULL;
    TAILQ_INSERT_HEAD(&list->list, item, entries);
}

void list_insert_tail(list_t *list, void *data)
{
    item_t *item = (item_t *)malloc(sizeof(item_t));
    item->data = data;
    item->entries.tqe_next = NULL;
    item->entries.tqe_prev = NULL;
    TAILQ_INSERT_TAIL(&list->list, item, entries);
}

void list_remove_current(list_t *list)
{
    if(list->current)
    {
        log_error(MSG());
        abort();
    }
    item_t *next = TAILQ_NEXT(list->current, entries);
    if(!next)
        next = TAILQ_PREV(list->current, list_head_s, entries);
    TAILQ_REMOVE(&list->list, list->current, entries);
    list->current = next;
}

void list_remove_item(list_t *list, void *data)
{
    for(list_first(list); list_is_data(list); list_next(list))
    {
        if(data == list_data(list))
        {
            list_remove_current(list);
            return;
        }
    }
}
