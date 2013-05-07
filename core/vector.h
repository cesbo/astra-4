/*
 * Astra Core
 * http://cesbo.com
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#ifndef _VECTOR_H_
#define _VECTOR_H_ 1

#include "base.h"

typedef struct asc_vector_t asc_vector_t;

asc_vector_t * asc_vector_init(void) __wur;
void asc_vector_destroy(asc_vector_t * vec);

void * asc_vector_get_dataptr(asc_vector_t * vec);
int asc_vector_size(asc_vector_t * vec);
void asc_vector_clear(asc_vector_t * vec);
void asc_vector_append_end(asc_vector_t * vec, void * data, int len);
void asc_vector_remove_begin(asc_vector_t * vec, int len);
void asc_vector_remove_end(asc_vector_t * vec, int len);

void asc_vector_test(void);


#endif /* _VECTOR_H_ */
