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
typedef void (*event_callback_t)(void *);

void asc_event_core_init(void);
void asc_event_core_loop(void);
void asc_event_core_destroy(void);


asc_event_t * asc_event_init(int fd, event_callback_t callback_read, event_callback_t callback_write, event_callback_t callback_error, void *arg) __wur;
void asc_event_set_read(asc_event_t * event, event_callback_t callback_read);
void asc_event_cancel_read(asc_event_t * event);
void asc_event_set_write(asc_event_t * event, event_callback_t callback_write);
void asc_event_cancel_write(asc_event_t * event);
void asc_event_set_error(asc_event_t * event, event_callback_t callback_error);
void asc_event_cancel_error(asc_event_t * event);
void asc_event_set_arg(asc_event_t * event, void *arg);
void asc_event_close(asc_event_t *event);


/*OLDS! */
asc_event_t * asc_event_on_read(int fd, event_callback_t callback, void *arg) __wur;
asc_event_t * asc_event_on_write(int fd, event_callback_t callback, void *arg) __wur;
#endif /* _EVENT_H_ */
