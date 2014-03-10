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

#include "utils.h"

uint64_t asc_utime(void)
{
#ifdef HAVE_CLOCK_GETTIME
    struct timespec ts;

    if(clock_gettime(CLOCK_MONOTONIC, &ts) == EINVAL)
        (void)clock_gettime(CLOCK_REALTIME, &ts);

    return ((uint64_t)ts.tv_sec * 1000000) + (uint64_t)(ts.tv_nsec / 1000);
#else
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return ((uint64_t)tv.tv_sec * 1000000) + (uint64_t)tv.tv_usec;
#endif
}

/*
 *  oooooooo8 ooooooooooo oooooooooo  ooooo oooo   oooo  ooooooo8
 * 888        88  888  88  888    888  888   8888o  88 o888    88
 *  888oooooo     888      888oooo88   888   88 888o88 888    oooo
 *         888    888      888  88o    888   88   8888 888o    88
 * o88oooo888    o888o    o888o  88o8 o888o o88o    88  888ooo888
 *
 */

string_buffer_t * string_buffer_alloc(void)
{
    string_buffer_t *buffer = malloc(sizeof(string_buffer_t));
    buffer->size = 0;
    buffer->last = buffer;
    buffer->next = NULL;
    return buffer;
}

void string_buffer_addchar(string_buffer_t *buffer, char c)
{
    string_buffer_t *last = buffer->last;
    if(last->size + 1 > (ssize_t)sizeof(buffer->buffer))
    {
        last->next = malloc(sizeof(string_buffer_t));
        last = last->next;
        last->size = 0;
        last->last = NULL;
        last->next = NULL;
        buffer->last = last;
    }

    last->buffer[last->size] = c;
    ++last->size;
}

void string_buffer_addlstring(string_buffer_t *buffer, const char *str, int size)
{
    string_buffer_t *last = buffer->last;

    if(last->size + size > (ssize_t)sizeof(buffer->buffer))
    {
        const int cap = sizeof(buffer->buffer) - last->size;
        if(cap > 0)
        {
            memcpy(&last->buffer[last->size], str, cap);
            last->size += cap;
        }

        last->next = malloc(sizeof(string_buffer_t));
        last = last->next;
        last->size = 0;
        last->last = NULL;
        last->next = NULL;
        buffer->last = last;
        string_buffer_addlstring(buffer, &str[cap], size - cap);
        return;
    }
    else
    {
        memcpy(&last->buffer[last->size], str, size);
        last->size += size;
    }
}

void string_buffer_push(lua_State *L, string_buffer_t *buffer)
{
    luaL_Buffer b;
    luaL_buffinit(L, &b);

    string_buffer_t *next_next;
    for(string_buffer_t *next = buffer
        ; next && (next_next = next->next, 1)
        ; next = next_next)
    {
        luaL_addlstring(&b, next->buffer, next->size);
        free(next);
    }

    luaL_pushresult(&b);
}

void string_buffer_free(string_buffer_t *buffer)
{
    string_buffer_t *next_next;
    for(string_buffer_t *next = buffer
        ; next && (next_next = next->next, 1)
        ; next = next_next)
    {
        free(next);
    }
}
