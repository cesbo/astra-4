/*
 * Astra Core
 * http://cesbo.com
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
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
