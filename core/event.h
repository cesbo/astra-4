/*
 * Astra Core
 * http://cesbo.com
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Revised 2013 Krasheninnikov Alexander
 * Licensed under the MIT license.
 */

#ifndef _EVENT_H_
#define _EVENT_H_ 1

#include "base.h"

typedef struct asc_event_t asc_event_t;
typedef void (*event_callback_t)(void *);

void asc_event_core_init(void);
void asc_event_core_loop(void);
void asc_event_core_destroy(void);

asc_event_t * asc_event_init(int fd, void *arg) __wur;
void asc_event_set_on_read(asc_event_t *event, event_callback_t on_read);
void asc_event_set_on_write(asc_event_t *event, event_callback_t on_write);
void asc_event_set_on_error(asc_event_t *event, event_callback_t on_error);

void asc_event_close(asc_event_t *event);

#endif /* _EVENT_H_ */
