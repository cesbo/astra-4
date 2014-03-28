/*
 * Astra Core
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2014, Andrey Dyldin <and@cesbo.com>
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

#include "assert.h"
#include "thread.h"
#include "list.h"
#include "log.h"

#ifdef _WIN32
#   include <windows.h>
#else
#   include <pthread.h>
#endif

extern bool is_main_loop_idle;

#define MSG(_msg) "[core/thread] " _msg

struct asc_thread_buffer_t
{
    uint8_t *buffer;
    size_t size;
    size_t read;
    size_t write;
    size_t count;

#ifdef _WIN32
    HANDLE mutex;
#else
    pthread_mutex_t mutex;
#endif
};

struct asc_thread_t
{
    thread_callback_t loop;
    thread_callback_t on_read;
    thread_callback_t on_close;

    asc_thread_buffer_t *buffer; // on_read
    void *arg;

    bool is_started;
    bool is_closed;

#ifdef _WIN32
    HANDLE thread;
#else
    pthread_t thread;
#endif
};

typedef struct
{
    asc_list_t *thread_list;
    bool is_changed;
} thread_observer_t;

static thread_observer_t thread_observer;

#ifdef _WIN32
#   define asc_thread_mutex_init(_mutex) _mutex = CreateMutex(NULL, FALSE, NULL)
#   define asc_thread_mutex_destroy(_mutex) CloseHandle(_mutex)
#   define asc_thread_mutex_lock(_mutex) WaitForSingleObject(_mutex, INFINITE)
#   define asc_thread_mutex_unlock(_mutex) ReleaseMutex(_mutex)
#else
#   define asc_thread_mutex_init(_mutex) pthread_mutex_init(&_mutex, NULL)
#   define asc_thread_mutex_destroy(_mutex) pthread_mutex_destroy(&_mutex)
#   define asc_thread_mutex_lock(_mutex) pthread_mutex_lock(&_mutex)
#   define asc_thread_mutex_unlock(_mutex) pthread_mutex_unlock(&_mutex)
#endif

void asc_thread_core_init(void)
{
    memset(&thread_observer, 0, sizeof(thread_observer));
    thread_observer.thread_list = asc_list_init();
}

void asc_thread_core_destroy(void)
{
    asc_thread_t *prev_thread = NULL;
    for(asc_list_first(thread_observer.thread_list)
        ; !asc_list_eol(thread_observer.thread_list)
        ; asc_list_first(thread_observer.thread_list))
    {
        asc_thread_t *thread = asc_list_data(thread_observer.thread_list);
        asc_assert(thread != prev_thread
                   , MSG("loop on asc_thread_core_destroy() thread:%p")
                   , thread);
        if(thread->on_close)
            thread->on_close(thread->arg);
        prev_thread = thread;
    }

    asc_list_destroy(thread_observer.thread_list);
    thread_observer.thread_list = NULL;
}

void asc_thread_core_loop(void)
{
    thread_observer.is_changed = false;
    asc_list_for(thread_observer.thread_list)
    {
        asc_thread_t *thread = asc_list_data(thread_observer.thread_list);
        if(!thread->is_started)
            continue;

        if(thread->on_read)
        {
            if(thread->buffer->count > 0)
            {
                is_main_loop_idle = false;
                thread->on_read(thread->arg);
                if(thread_observer.is_changed)
                    break;
            }
        }

        if(thread->on_close && thread->is_closed)
        {
            is_main_loop_idle = false;
            thread->on_close(thread->arg);
            if(thread_observer.is_changed)
                break;
        }
    }
}


asc_thread_t * asc_thread_init(void *arg)
{
    asc_thread_t *thread = calloc(1, sizeof(asc_thread_t));

    thread->arg = arg;

    asc_list_insert_tail(thread_observer.thread_list, thread);
    thread_observer.is_changed = true;

    return thread;
}

void asc_thread_set_on_read(asc_thread_t *thread
                            , asc_thread_buffer_t *buffer
                            , thread_callback_t on_read)
{
    thread->buffer = buffer;
    thread->on_read = on_read;
}

void asc_thread_set_on_close(asc_thread_t *thread, thread_callback_t on_close)
{
    thread->on_close = on_close;
}

#ifdef _WIN32
DWORD WINAPI asc_thread_loop(void *arg)
#else
static void * asc_thread_loop(void *arg)
#endif
{
    asc_thread_t *thread = arg;

    thread->is_started = true;
    thread->loop(thread->arg);
    thread->is_closed = true;

#ifdef _WIN32
    return 0;
#else
    pthread_exit(NULL);
#endif
}

void asc_thread_start(asc_thread_t *thread, thread_callback_t loop)
{
    thread->loop = loop;

    asc_assert(thread->loop != NULL, MSG("loop required"));
    asc_assert(thread->on_close != NULL, MSG("on_close required"));

#ifdef _WIN32
    DWORD tid;
    thread->thread = CreateThread(NULL, 0, &asc_thread_loop, thread, 0, &tid);
    if(thread->thread != NULL)
        return;
#else
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    const int ret = pthread_create(&thread->thread, &attr, asc_thread_loop, thread);
    pthread_attr_destroy(&attr);
    if(ret == 0)
        return;
#endif

    asc_assert(0, MSG("failed to start thread"));
}

void asc_thread_close(asc_thread_t *thread)
{
    if(!thread)
        return;

    thread->is_closed = true;

#ifdef _WIN32
    WaitForSingleObject(thread->thread, INFINITE);
    CloseHandle(thread->thread);
#else
    pthread_join(thread->thread, NULL);
#endif

    thread_observer.is_changed = true;
    asc_list_remove_item(thread_observer.thread_list, thread);

    free(thread);
}

asc_thread_buffer_t * asc_thread_buffer_init(size_t size)
{
    asc_thread_buffer_t * buffer = calloc(1, sizeof(asc_thread_buffer_t));
    buffer->size = size;
    buffer->buffer = malloc(size);
    asc_thread_mutex_init(buffer->mutex);
    return buffer;
}

void asc_thread_buffer_destroy(asc_thread_buffer_t *buffer)
{
    if(!buffer)
        return;
    free(buffer->buffer);
    asc_thread_mutex_destroy(buffer->mutex);
    free(buffer);
}

void asc_thread_buffer_flush(asc_thread_buffer_t *buffer)
{
    asc_thread_mutex_lock(buffer->mutex);
    buffer->count = 0;
    buffer->read = 0;
    buffer->write = 0;
    asc_thread_mutex_unlock(buffer->mutex);
}

ssize_t asc_thread_buffer_read(asc_thread_buffer_t *buffer, void *data, size_t size)
{
    if(!size)
        return 0;

    asc_thread_mutex_lock(buffer->mutex);
    if(buffer->count < size)
    {
        asc_thread_mutex_unlock(buffer->mutex);
        return 0;
    }

    if(buffer->read + size >= buffer->size)
    {
        const size_t tail = buffer->size - buffer->read;
        memcpy(data, &buffer->buffer[buffer->read], tail);
        buffer->read = size - tail;
        if(buffer->read > 0)
            memcpy(&((uint8_t *)data)[tail], buffer->buffer, buffer->read);
    }
    else
    {
        memcpy(data, &buffer->buffer[buffer->read], size);
        buffer->read += size;
    }
    buffer->count -= size;
    asc_thread_mutex_unlock(buffer->mutex);

    return size;
}

ssize_t asc_thread_buffer_write(asc_thread_buffer_t *buffer, const void *data, size_t size)
{
    if(!size)
        return 0;

    asc_thread_mutex_lock(buffer->mutex);
    if(buffer->count + size > buffer->size)
    {
        asc_thread_mutex_unlock(buffer->mutex);
        return -1; // buffer overflow
    }

    if(buffer->write + size >= buffer->size)
    {
        const size_t tail = buffer->size - buffer->write;
        memcpy(&buffer->buffer[buffer->write], data, tail);
        buffer->write = size - tail;
        if(buffer->write > 0)
            memcpy(buffer->buffer, &((uint8_t *)data)[tail], buffer->write);
    }
    else
    {
        memcpy(&buffer->buffer[buffer->write], data, size);
        buffer->write += size;
    }
    buffer->count += size;
    asc_thread_mutex_unlock(buffer->mutex);

    return size;
}
