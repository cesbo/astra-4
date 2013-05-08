/*
 * Astra Core
 * http://cesbo.com
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#ifndef _EVENT_H_
#define _EVENT_H_ 1

#include "base.h"

typedef struct asc_event_t asc_event_t;
typedef void (*event_callback_func_t)(void *, int);

void asc_event_core_init(void);
void asc_event_core_loop(void);
void asc_event_core_destroy(void);

asc_event_t * asc_event_on_read(int fd, event_callback_func_t callback, void *arg) __wur;
asc_event_t * asc_event_on_write(int fd, event_callback_func_t callback, void *arg) __wur;
void asc_event_close(asc_event_t *event);

#endif /* _EVENT_H_ */
