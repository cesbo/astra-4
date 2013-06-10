/*
 * Astra Core
 * http://cesbo.com
 *
 * Copyright (C) 2013, Krasheninnikov Alexander
 * Licensed under the MIT license.
 */

#include "assert.h"
#include "vector.h"
#include "log.h"

struct asc_vector_t
{
    int element_size;/* One element size */
    char *data; /* always != NULL while vector is inited */
    int skip_bytes; /* how many bytes are useless in front of buffer, measured in bytes */
    int size_bytes; /* currently used size, measured in bytes */
    int capacity_bytes; /* data elements allocated, measured in bytes */
    /* buffer is:
     * (skip) + (size) + (unused) = capacity
     */
};

#define VECTOR_CAPACITY_MIN 128

/* capacity is multiplied by FACTOR until it reaches MAX_FACTORED,
 * after it grows by adding MAX_FACTORED_STEP
 */

#define VECTOR_CAPACITY_FACTOR 2
#define VECTOR_CAPACITY_MAX_FACTORED (16 * 1024 * 1024)
#define VECTOR_CAPACITY_MAX_FACTORED_STEP (16 * 1024 * 1024)

static inline int asc_vector_get_capacity_next(int capacity)
{
    if(capacity < VECTOR_CAPACITY_MAX_FACTORED)
        return capacity * VECTOR_CAPACITY_FACTOR;
    else
        return capacity + VECTOR_CAPACITY_MAX_FACTORED_STEP;
}

static inline int asc_vector_get_capacity_prev(int capacity)
{
    if(capacity > VECTOR_CAPACITY_MAX_FACTORED)
        return capacity - VECTOR_CAPACITY_MAX_FACTORED_STEP;
    else
    {
        if(capacity > VECTOR_CAPACITY_MIN)
            return capacity / VECTOR_CAPACITY_FACTOR;
        else
            return VECTOR_CAPACITY_MIN;
    }
}

static void asc_vector_realloc(asc_vector_t *vec, int newCap, int copySize)
{
    /* usual realloc is bad idea, because we do not know what happens if it returns NULL */
    void * newData = malloc(newCap);
    asc_assert(newData, "[core/vector] memory allocation failed, "
                        "cannot allocate %d bytes"
               , newCap);
    memcpy(newData, vec->data + vec->skip_bytes, copySize);
    free(vec->data);
    vec->data = newData;
    vec->skip_bytes = 0;
    vec->capacity_bytes = newCap;
}

static void asc_vector_grow(asc_vector_t *vec, int add_size)
{
    int newSize = vec->skip_bytes + vec->size_bytes + add_size;
    if(newSize <= vec->capacity_bytes)
        return; /* no need to grow, because have enough capacity */

    int newCap = vec->capacity_bytes;
    /* Determine target capacity to grow to. It should be big enough to fit newSize */
    while(newSize > newCap)
        newCap = asc_vector_get_capacity_next(newCap);
    asc_vector_realloc(vec, newCap, vec->size_bytes);
}

static void asc_vector_shrink(asc_vector_t *vec, int del_size)
{
    int newSize = vec->skip_bytes + vec->size_bytes - del_size;
    asc_assert(newSize >= 0, "[core/vector] cannot shrink vector by %d bytes, "
                             "because it has only %d bytes"
               , del_size, vec->size_bytes);
    int newCap = asc_vector_get_capacity_prev(vec->capacity_bytes);
    if(newSize > newCap)
        return; /* No need to shrink.
                 * It will be done only when newSize will be less
                 * than previous step capacity
                 */

    /* Determine target capacity to shrink to.
     * It should be one step bigger than it is requiered - hysteresis
     */
    while(1)
    {
        int newCap2 = asc_vector_get_capacity_prev(newCap);
        if(newCap2 == newCap)
            break; /* minimum reached */
        if(newSize > newCap2)
            break; /* new size doesn't fit into new cap */
        newCap = newCap2;
    }
    asc_vector_realloc(vec, newCap, vec->size_bytes - del_size);
}

asc_vector_t * asc_vector_init(int element_size)
{
    asc_vector_t *vec = malloc(sizeof(asc_vector_t));
    asc_assert(vec, "[core/vector] memory allocation failed, "
                    "cannot allocate %d bytes for vector"
               , sizeof(asc_vector_t));
    vec->element_size = element_size;
    vec->skip_bytes = 0;
    vec->size_bytes = 0;
    vec->capacity_bytes = VECTOR_CAPACITY_MIN;
    vec->data = malloc(vec->capacity_bytes);
    asc_assert(vec->data, "[core/vector] memory allocation failed, "
                          "cannot allocate %d bytes"
               , vec->capacity_bytes);
    return vec;
}

void asc_vector_destroy(asc_vector_t *vec)
{
    free(vec->data);
    free(vec);
}

void * asc_vector_get_dataptr(asc_vector_t *vec)
{
    return vec->data + vec->skip_bytes;
}

void * asc_vector_get_dataptr_at(asc_vector_t *vec, int pos_elem)
{
    asc_assert(pos_elem >= 0, "[core/vector] cannot get with negative pos");
    asc_assert(pos_elem * vec->element_size < vec->size_bytes
               , "[core/vector] cannot get with pos after end");

    return vec->data + vec->skip_bytes + pos_elem * vec->element_size;
}

inline int asc_vector_count(asc_vector_t *vec)
{
    return vec->size_bytes / vec->element_size;
}

void asc_vector_clear(asc_vector_t *vec)
{
    asc_vector_remove_end(vec, asc_vector_count(vec));
}

void asc_vector_resize(asc_vector_t *vec, int count_elem)
{
    int new_size_bytes = count_elem * vec->element_size;
    if(new_size_bytes == vec->size_bytes)
        return;
    if(new_size_bytes < vec->size_bytes)
        asc_vector_shrink(vec, vec->size_bytes - new_size_bytes);
    else
        asc_vector_grow(vec, new_size_bytes - vec->size_bytes);
    vec->size_bytes = new_size_bytes;
}

void asc_vector_append_end(asc_vector_t *vec, void *data, int count_elem)
{
    asc_vector_insert_middle(vec, asc_vector_count(vec), data, count_elem);
}

void asc_vector_insert_middle(asc_vector_t *vec, int pos_elem, void *data, int count_elem)
{
    int len = count_elem * vec->element_size;
    int pos = pos_elem * vec->element_size;

    asc_assert(data, "[core/vector] cannot insert_middle with NULL data");
    asc_assert(count_elem >= 0, "[core/vector] cannot insert_middle with negative count");
    asc_assert((pos >= 0) && (pos <= vec->size_bytes)
               , "[core/vector] cannot insert_middle at invalid pos_elem %d"
               , pos_elem);
    if(len == 0)
        return;
    asc_vector_grow(vec, len);
    if(pos < vec->size_bytes)/* need to move contents */
    {
        memmove(vec->data + vec->skip_bytes + pos + len
                , vec->data + vec->skip_bytes + pos
                , vec->size_bytes - pos);
    }
    asc_assert(vec->capacity_bytes >= (vec->skip_bytes + pos + len)
               , "Memcpy after end, cap %d, skip %d, pos %d len %d, sz %d"
               , vec->capacity_bytes, vec->skip_bytes, pos, len, vec->size_bytes);
    memcpy(vec->data + vec->skip_bytes + pos, data, len);
    vec->size_bytes += len;
}

void asc_vector_remove_begin(asc_vector_t *vec, int count_elem)
{
    asc_vector_remove_middle(vec, 0, count_elem);
}

void asc_vector_remove_middle(asc_vector_t *vec, int pos_elem, int count_elem)
{
    int len = count_elem * vec->element_size;
    int pos = pos_elem * vec->element_size;

    asc_assert(count_elem >= 0, "[core/vector] cannot remove_middle with negative count");
    asc_assert((pos >= 0) && (pos <= vec->size_bytes)
               , "[core/vector] cannot remove_middle with negative len");
    asc_assert((pos + len) <= vec->size_bytes
               , "[core/vector] cannot remove_middle with pos + len > size : %d + %d > %d"
               , pos, len, vec->size_bytes);

    if(len == 0)
        return;

    if(pos == 0)
        vec->skip_bytes += len;
    else if((pos + len) < vec->size_bytes) /* need to move... */
    {
        memmove(vec->data + vec->skip_bytes + pos
                , vec->data + vec->skip_bytes + pos + len
                , vec->size_bytes - pos - len);
    }
    asc_vector_shrink(vec, len);
    vec->size_bytes -= len;
}

void asc_vector_remove_end(asc_vector_t *vec, int count_elem)
{
    asc_vector_remove_middle(vec, asc_vector_count(vec) - count_elem, count_elem);
}

/* TEST funcs */
#if 0
void asc_vector_test(void)
{
    asc_vector_t * vec = asc_vector_init(1);
    int cap = vec->capacity_bytes;
    time_t now;
    time(&now);
    printf("Time: %s", ctime(&now));

    printf("Initial Size %d, capacity %d, skip %d\n"
           , vec->size_bytes, vec->capacity_bytes, vec->skip_bytes);
    int m = 4000000;
    for (int i = 0; i < m; ++i)
    {
        char c[1000];
        c[0] = (char)i;
        asc_vector_append_end(vec, &c, 100);
        int newCap = vec->capacity_bytes;
        if(((i % 100000) != 0) && (newCap == cap))
            continue;
        cap = newCap;
        printf("Size %d, capacity %d, skip %d\n"
               , vec->size_bytes, vec->capacity_bytes, vec->skip_bytes);
    }
    printf("Intermediate Size %d, capacity %d, skip %d\n"
           , vec->size_bytes, vec->capacity_bytes, vec->skip_bytes);

    for(int i = 0; i < m; ++i)
    {
        asc_assert( ((char*)asc_vector_get_dataptr(vec)) [0] == (char)i
                   , "Invalid data in vector, size %d", vec->size_bytes);
        asc_vector_remove_begin(vec, 100);
        int newCap = vec->capacity_bytes;
        if(((i % 100000) != 0) && (newCap == cap))
            continue;
        cap = newCap;
        printf("Size %d, capacity %d, skip %d\n"
               , vec->size_bytes, vec->capacity_bytes, vec->skip_bytes);
    }
    printf("Final Size %d, capacity %d, skip %d\n"
           , vec->size_bytes, vec->capacity_bytes, vec->skip_bytes);

    asc_vector_destroy(vec);

    time(&now);
    printf("Time: %s", ctime(&now));
}
#endif

/*
 * ptvector
 */

#define PTRSIZE (int)(sizeof(void*))

asc_ptrvector_t * asc_ptrvector_init(void)
{
    return asc_vector_init(PTRSIZE);
}

void asc_ptrvector_destroy(asc_ptrvector_t *vec)
{
    asc_vector_destroy(vec);
}

void * asc_ptrvector_get_at(asc_ptrvector_t *vec, int pos)
{
    void * data = asc_vector_get_dataptr_at(vec, pos);
    void * res = *((void**)data);
    return res;
}

int asc_ptrvector_count(asc_ptrvector_t *vec)
{
    return asc_vector_count(vec);
}

void asc_ptrvector_clear(asc_ptrvector_t *vec)
{
    asc_vector_clear(vec);
}

void asc_ptrvector_append_end(asc_ptrvector_t *vec, void * ptr)
{
    asc_vector_append_end(vec, &ptr, 1);
}

void asc_ptrvector_insert_middle(asc_ptrvector_t *vec, int pos, void * ptr)
{
    asc_vector_insert_middle(vec, pos, &ptr, 1);
}

void asc_ptrvector_remove_begin(asc_ptrvector_t *vec)
{
    asc_vector_remove_begin(vec, 1);
}

void asc_ptrvector_remove_middle(asc_ptrvector_t *vec, int pos)
{
    asc_vector_remove_middle(vec, pos, 1);
}

void asc_ptrvector_remove_end(asc_ptrvector_t *vec)
{
    asc_vector_remove_end(vec, 1);
}

