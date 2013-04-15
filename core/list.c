/*
 * Astra Core
 * http://cesbo.com
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#include "list.h"
#include "log.h"

#include <sys/queue.h>

#define MSG() "[core/list] %s():%d", __FUNCTION__, __LINE__

typedef struct item_s
{
    void *data;
    TAILQ_ENTRY(item_s) entries;
} item_t;

struct asc_list_t
{
    struct item_s *current;
    TAILQ_HEAD(list_head_s, item_s) list;
};

asc_list_t * asc_list_init(void)
{
    asc_list_t *list = malloc(sizeof(asc_list_t));
    TAILQ_INIT(&list->list);
    list->current = NULL;
    return list;
}

void asc_list_destroy(asc_list_t *list)
{
    if(list->current)
    {
        asc_log_error(MSG());
        abort();
    }
    free(list);
}

inline void asc_list_first(asc_list_t *list)
{
    list->current = TAILQ_FIRST(&list->list);
}

inline void asc_list_next(asc_list_t *list)
{
    if(list->current)
        list->current = TAILQ_NEXT(list->current, entries);
}

inline int asc_list_eol(asc_list_t *list)
{
    return (list->current != NULL);
}

inline void * asc_list_data(asc_list_t *list)
{
    return list->current->data;
}

void asc_list_insert_head(asc_list_t *list, void *data)
{
    item_t *item = malloc(sizeof(item_t));
    item->data = data;
    item->entries.tqe_next = NULL;
    item->entries.tqe_prev = NULL;
    TAILQ_INSERT_HEAD(&list->list, item, entries);
}

void asc_list_insert_tail(asc_list_t *list, void *data)
{
    item_t *item = malloc(sizeof(item_t));
    item->data = data;
    item->entries.tqe_next = NULL;
    item->entries.tqe_prev = NULL;
    TAILQ_INSERT_TAIL(&list->list, item, entries);
}

void asc_list_remove_current(asc_list_t *list)
{
    if(!list->current)
    {
        asc_log_error(MSG());
        abort();
    }
    item_t *next = TAILQ_NEXT(list->current, entries);
    TAILQ_REMOVE(&list->list, list->current, entries);
    list->current = next;
}

void asc_list_remove_item(asc_list_t *list, void *data)
{
    for(asc_list_first(list); asc_list_eol(list); asc_list_next(list))
    {
        if(data == asc_list_data(list))
        {
            asc_list_remove_current(list);
            return;
        }
    }
}
