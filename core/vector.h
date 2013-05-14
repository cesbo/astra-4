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

/* Vector of bytes */

typedef struct asc_vector_t asc_vector_t;

asc_vector_t * asc_vector_init(int element_size) __wur;
void asc_vector_destroy(asc_vector_t * vec);

void * asc_vector_get_dataptr(asc_vector_t * vec);
void * asc_vector_get_dataptr_at(asc_vector_t * vec, int pos_elem);
int asc_vector_count(asc_vector_t * vec);/* returns count of elements */
void asc_vector_clear(asc_vector_t * vec);
void asc_vector_append_end(asc_vector_t * vec, void * data, int count_elem);
void asc_vector_insert_middle(asc_vector_t * vec, int pos_elem, void * data, int count_elem);
void asc_vector_remove_begin(asc_vector_t * vec, int count_elem);
void asc_vector_remove_middle(asc_vector_t * vec, int pos, int count_elem);
void asc_vector_remove_end(asc_vector_t * vec, int count_elem);

void asc_vector_test(void);

/* Vector of pointers */
typedef asc_vector_t asc_ptrvector_t;

asc_ptrvector_t * asc_ptrvector_init(void) __wur;
void asc_ptrvector_destroy(asc_ptrvector_t * vec);

void * asc_ptrvector_get_at(asc_ptrvector_t * vec, int pos);
int asc_ptrvector_count(asc_ptrvector_t * vec);
void asc_ptrvector_clear(asc_ptrvector_t * vec);
void asc_ptrvector_append_end(asc_ptrvector_t * vec, void * ptr);
void asc_ptrvector_insert_middle(asc_ptrvector_t * vec, int pos, void * ptr);
void asc_ptrvector_remove_begin(asc_ptrvector_t * vec);
void asc_ptrvector_remove_middle(asc_ptrvector_t * vec, int pos);
void asc_ptrvector_remove_end(asc_ptrvector_t * vec);


#endif /* _VECTOR_H_ */
