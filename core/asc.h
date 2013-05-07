/*
 * Astra Core
 * http://cesbo.com
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#ifndef _ASC_H_
#define _ASC_H_ 1

#include "assert.h"
#include "base.h"
#include "event.h"
#include "list.h"
#include "vector.h"
#include "log.h"
#include "socket.h"
#include "stream.h"
#include "thread.h"
#include "timer.h"

#define ASC_INIT()                                                                              \
    srand((uint32_t)time(NULL));                                                                \
    asc_timer_core_init();                                                                      \
    asc_socket_core_init();                                                                     \
    asc_event_core_init();

#define ASC_LOOP()                                                                              \
    while(1)                                                                                    \
    {                                                                                           \
        asc_event_core_loop();                                                                  \
        asc_timer_core_loop();                                                                  \
    }

#define ASC_DESTROY()                                                                           \
    asc_event_core_destroy();                                                                   \
    asc_socket_core_destroy();                                                                  \
    asc_timer_core_destroy();                                                                   \
    asc_log_info("[main] exit");                                                                \
    asc_log_core_destroy();

#endif /* _ASC_H_ */
