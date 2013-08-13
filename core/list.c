/*
 * Astra Core
 * http://cesbo.com/en/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "assert.h"
#include "list.h"
#include "log.h"

typedef struct item_s
{
    void *data;
    TAILQ_ENTRY(item_s) entries;
} item_t;

struct asc_list_t
{
    size_t size;
    struct item_s *current;
    TAILQ_HEAD(list_head_s, item_s) list;
};

asc_list_t * asc_list_init(void)
{
    asc_list_t *list = malloc(sizeof(asc_list_t));
    TAILQ_INIT(&list->list);
    list->size = 0;
    list->current = NULL;
    return list;
}

void asc_list_destroy(asc_list_t *list)
{
    asc_assert(list->current == NULL, "[core/list] list is not empty");
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
    return (list->current == NULL);
}

inline void * asc_list_data(asc_list_t *list)
{
    asc_assert(list->current != NULL, "[core/list] failed to get data");
    return list->current->data;
}

inline size_t asc_list_size(asc_list_t *list)
{
    return list->size;
}

void asc_list_insert_head(asc_list_t *list, void *data)
{
    ++list->size;
    item_t *item = malloc(sizeof(item_t));
    item->data = data;
    item->entries.tqe_next = NULL;
    item->entries.tqe_prev = NULL;
    TAILQ_INSERT_HEAD(&list->list, item, entries);
}

void asc_list_insert_tail(asc_list_t *list, void *data)
{
    ++list->size;
    item_t *item = malloc(sizeof(item_t));
    item->data = data;
    item->entries.tqe_next = NULL;
    item->entries.tqe_prev = NULL;
    TAILQ_INSERT_TAIL(&list->list, item, entries);
}

void asc_list_remove_current(asc_list_t *list)
{
    --list->size;
    asc_assert(list->current != NULL, "[core/list] failed to remove item");
    item_t *next = TAILQ_NEXT(list->current, entries);
    TAILQ_REMOVE(&list->list, list->current, entries);
    free(list->current);
    list->current = next;
}

void asc_list_remove_item(asc_list_t *list, void *data)
{
    for(asc_list_first(list); !asc_list_eol(list); asc_list_next(list))
    {
        if(data == asc_list_data(list))
        {
            asc_list_remove_current(list);
            return;
        }
    }
}
