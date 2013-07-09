/*
 * Astra Core
 * http://cesbo.com
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#ifndef _LIST_H_
#define _LIST_H_ 1

#include "base.h"

typedef struct asc_list_t asc_list_t;

asc_list_t * asc_list_init(void) __wur;
void asc_list_destroy(asc_list_t *list);

void asc_list_first(asc_list_t *list);
void asc_list_next(asc_list_t *list);
int asc_list_eol(asc_list_t *list) __wur;
void * asc_list_data(asc_list_t *list) __wur;
size_t asc_list_size(asc_list_t *list) __wur;

void asc_list_insert_head(asc_list_t *list, void *data);
void asc_list_insert_tail(asc_list_t *list, void *data);

void asc_list_remove_current(asc_list_t *list);
void asc_list_remove_item(asc_list_t *list, void *data);

#define asc_list_for(__list) \
    for(asc_list_first(__list); !asc_list_eol(__list); asc_list_next(__list))

#endif /* _LIST_H_ */
