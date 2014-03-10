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

#ifndef _UTILS_H_
#define _UTILS_H_ 1

#include "base.h"
#include <lua/lauxlib.h>

uint64_t asc_utime(void);

#define MAX_BUFFER_SIZE 4096

typedef struct string_buffer_t string_buffer_t;

struct string_buffer_t
{
    char buffer[4096];
    int size;

    string_buffer_t *last;
    string_buffer_t *next;
};

string_buffer_t * string_buffer_alloc(void);
void string_buffer_addchar(string_buffer_t *buffer, char c);
void string_buffer_addlstring(string_buffer_t *buffer, const char *str, int size);
void string_buffer_push(lua_State *L, string_buffer_t *buffer);
void string_buffer_free(string_buffer_t *buffer);

#endif /* _UTILS_H_ */
