/*
 * Astra Core
 * http://cesbo.com/astra
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
