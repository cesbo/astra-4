/*
 * Astra Core
 * http://cesbo.com
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#include "assert.h"
#include "vector.h"
#include "log.h"

struct asc_vector_t
{
	char * data; /* always != NULL while vector is inited */
	int skip; /* how many bytes are useless in front of buffer */
	int size; /* currently used size */
	int capacity; /* data elements allocated */
	/* buffer is: 
	  (skip) + (size) + (unused) = capacity
	*/
};

#define VECTOR_CAPACITY_MIN 128

/* capacity is multiplied by FACTOR until it reaches MAX_FACTORED, after it grows by adding MAX_FACTORED_STEP */

#define VECTOR_CAPACITY_FACTOR 2
#define VECTOR_CAPACITY_MAX_FACTORED 16*1024*1024
#define VECTOR_CAPACITY_MAX_FACTORED_STEP 16*1024*1024

static inline int asc_vector_get_capacity_next(int capacity)
{
    if (capacity < VECTOR_CAPACITY_MAX_FACTORED) 
        return capacity * VECTOR_CAPACITY_FACTOR;
    else
        return capacity + VECTOR_CAPACITY_MAX_FACTORED_STEP;
}

static inline int asc_vector_get_capacity_prev(int capacity)
{
    if (capacity > VECTOR_CAPACITY_MAX_FACTORED) 
        return capacity - VECTOR_CAPACITY_MAX_FACTORED_STEP;
    else
    {
        if (capacity > VECTOR_CAPACITY_MIN) 
            return capacity / VECTOR_CAPACITY_FACTOR;
        else
            return VECTOR_CAPACITY_MIN;
    }
}

static void asc_vector_realloc(asc_vector_t * vec, int newCap, int copySize)
{
    /* usual realloc is bad idea, because we do not know what happens if it returns NULL */
    void * newData = malloc(newCap);
    asc_assert(newData, "[core/vector] memory allocation failed, cannot allocate %d bytes", newCap);
    memcpy(newData, vec->data + vec->skip, copySize);
    free(vec->data);
    vec->data = newData;
    vec->skip = 0;
    vec->capacity = newCap;
}

static void asc_vector_grow(asc_vector_t * vec, int add_size)
{
    int newSize = vec->size + add_size;
    if (newSize <= vec->capacity) return;/* no need to grow, because have enough capacity */
    int newCap = vec->capacity;
    /* Determine target capacity to grow to. It should be big enough to fit newSize */
    while (newSize > newCap)
        newCap = asc_vector_get_capacity_next(newCap);
    asc_vector_realloc(vec, newCap, vec->size);
}

static void asc_vector_shrink(asc_vector_t * vec, int del_size)
{
    int newSize = vec->size - del_size;
    asc_assert(newSize >= 0, "[core/vector] cannot shrink vector by %d bytes, because it has only %d bytes", del_size, vec->size);
    int newCap = asc_vector_get_capacity_prev(vec->capacity);
    if (newSize > newCap) return;/* No need to shrink. It will be done only when newSize will be less than previous step capacity */
    /* Determine target capacity to shrink to. It should be one step bigger than it is requiered - hysteresis */
    while (1)
    {
        int newCap2 = asc_vector_get_capacity_prev(newCap);
        if (newCap2 == newCap) break;/* minimum reached */
        if (newSize > newCap2) break;/* new size doesn't fit into new cap */
        newCap = newCap2;
    };
    asc_vector_realloc(vec, newCap, newSize);
}

asc_vector_t * asc_vector_init(void)
{
    asc_vector_t * vec = malloc(sizeof(asc_vector_t));
    asc_assert(vec, "[core/vector] memory allocation failed, cannot allocate %d bytes for vector", sizeof(asc_vector_t));
    vec->skip = 0;
    vec->size = 0;
    vec->capacity = VECTOR_CAPACITY_MIN;
    vec->data = malloc(vec->capacity);
    asc_assert(vec->data, "[core/vector] memory allocation failed, cannot allocate %d bytes", vec->capacity);
    return vec;
}

void asc_vector_destroy(asc_vector_t * vec)
{
    free(vec->data);
    free(vec);
}

void * asc_vector_get_dataptr(asc_vector_t * vec)
{
    return vec->data + vec->skip;
}

int asc_vector_size(asc_vector_t * vec)
{
    return vec->size;
}

void asc_vector_clear(asc_vector_t * vec)
{
    asc_vector_remove_end(vec, vec->size);
}

void asc_vector_append_end(asc_vector_t * vec, void * data, int len)
{
    asc_assert(data, "[core/vector] cannot append with NULL data");
    asc_assert(len >= 0, "[core/vector] cannot append with negative len");
    if (len == 0) return;
    asc_vector_grow(vec, len);
    memcpy(vec->data + vec->size, data, len);
    vec->size += len;
}

void asc_vector_remove_begin(asc_vector_t * vec, int len)
{
    asc_assert(len >= 0, "[core/vector] cannot remove_begin with negative len");
    asc_assert(len <= vec->size, "[core/vector] cannot remove_begin with len > size : %d > %d", len, vec->size);
    if (len == 0) return;
        
    vec->skip += len;
    asc_vector_shrink(vec, len);
    vec->size -= len;
}

void asc_vector_remove_end(asc_vector_t * vec, int len)
{
    asc_assert(len >= 0, "[core/vector] cannot remove_end with negative len");
    asc_assert(len <= vec->size, "[core/vector] cannot remove_begin with len > size : %d > %d", len, vec->size);
    if (len == 0) return;

    asc_vector_shrink(vec, len);
    vec->size -= len;
}

/* TEST funcs */

void asc_vector_test(void)
{
    asc_vector_t * vec = asc_vector_init();
    int cap = vec->capacity;
    time_t now;
    time(&now);
    printf("Time: %s", ctime(&now));
  
    printf("Initial Size %d, capacity %d, skip %d\n", vec->size, vec->capacity, vec->skip);
    int m = 4000000;
    for (int i = 0; i < m; ++i)
    {
        char c[1000];
        c[0] = (char)i;
        asc_vector_append_end(vec, &c, 100);
        int newCap = vec->capacity;
        if (((i % 100000) != 0) && (newCap == cap)) continue;
        cap = newCap;
        printf("Size %d, capacity %d, skip %d\n", vec->size, vec->capacity, vec->skip);
    }
    printf("Intermediate Size %d, capacity %d, skip %d\n", vec->size, vec->capacity, vec->skip);
    
    for (int i = 0; i < m; ++i)
    {
        asc_assert( ((char*)asc_vector_get_dataptr(vec)) [0] == (char)i, "Invalid data in vector, size %d", vec->size);
        asc_vector_remove_begin(vec, 100);
        int newCap = vec->capacity;
        if (((i % 100000) != 0) && (newCap == cap)) continue;
        cap = newCap;
        printf("Size %d, capacity %d, skip %d\n", vec->size, vec->capacity, vec->skip);
    }
    printf("Final Size %d, capacity %d, skip %d\n", vec->size, vec->capacity, vec->skip);
  
    asc_vector_destroy(vec);

    time(&now);
    printf("Time: %s", ctime(&now));
}