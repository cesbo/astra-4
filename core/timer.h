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

#ifndef _TIMER_H_
#define _TIMER_H_ 1

#include "base.h"

typedef struct asc_timer_t asc_timer_t;

void asc_timer_core_init(void);
void asc_timer_core_loop(void);
void asc_timer_core_destroy(void);

void asc_timer_one_shot(unsigned int ms, void (*callback)(void *), void *arg);

asc_timer_t * asc_timer_init(unsigned int ms, void (*callback)(void *), void *arg) __wur;
void asc_timer_destroy(asc_timer_t *timer);

#endif /* _TIMER_H_ */
