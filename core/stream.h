/*
 * Astra Core
 * http://cesbo.com
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#ifndef _STREAM_H_
#define _STREAM_H_ 1

#include "base.h"

typedef struct asc_stream_t asc_stream_t;

asc_stream_t * asc_stream_init(void (*callback)(void *), void *arg);
void asc_stream_destroy(asc_stream_t *s);
ssize_t asc_stream_send(asc_stream_t *s, void *data, size_t size);
ssize_t asc_stream_recv(asc_stream_t *s, void *data, size_t size);

#endif /* _STREAM_H_ */
