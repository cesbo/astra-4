/*
 * Astra Core (String buffer)
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2015, Andrey Dyldin <and@cesbo.com>
 *                    2015, Artem Kharitonov <artem@sysert.ru>
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

#include "strbuffer.h"
#include <stdarg.h>

#define MAX_BUFFER_SIZE 4096

struct string_buffer_t
{
    char buffer[MAX_BUFFER_SIZE];
    size_t size;

    string_buffer_t *last;
    string_buffer_t *next;
};

string_buffer_t * string_buffer_alloc(void)
{
    string_buffer_t *buffer = (string_buffer_t *)malloc(sizeof(string_buffer_t));
    buffer->size = 0;
    buffer->last = buffer;
    buffer->next = NULL;
    return buffer;
}

/* only for single char operations */
static string_buffer_t * __string_buffer_last(string_buffer_t *buffer)
{
    string_buffer_t *last = buffer->last;
    if(last->size >= MAX_BUFFER_SIZE)
    {
        last->next = (string_buffer_t *)malloc(sizeof(string_buffer_t));
        last = last->next;
        last->size = 0;
        last->last = NULL;
        last->next = NULL;
        buffer->last = last;
    }
    return last;
}

void string_buffer_addchar(string_buffer_t *buffer, char c)
{
    string_buffer_t *last = __string_buffer_last(buffer);

    last->buffer[last->size] = c;
    ++last->size;
}

void string_buffer_addlstring(string_buffer_t *buffer, const char *str, size_t size)
{
    string_buffer_t *last = buffer->last;

    if(!size)
        size = strlen(str);

    size_t skip = 0;
    while(1)
    {
        const size_t cap = MAX_BUFFER_SIZE - last->size;
        const size_t rem = size - skip;
        if(cap >= rem)
        {
            memcpy(&last->buffer[last->size], &str[skip], rem);
            last->size += rem;
            return;
        }
        else
        {
            if(cap > 0)
            {
                memcpy(&last->buffer[last->size], &str[skip], cap);
                last->size += cap;
                skip += cap;
            }

            last->next = (string_buffer_t *)malloc(sizeof(string_buffer_t));
            last = last->next;
            last->size = 0;
            last->last = NULL;
            last->next = NULL;
            buffer->last = last;
        }
    }
}

void strung_buffer_addvastring(string_buffer_t *buffer, const char *str, va_list ap)
{
    string_buffer_t *last;

    bool is_zero;
    size_t length;

    int radix;
    bool is_sign;
    uint64_t number;
    int number_type;
    char number_str[32];
    size_t number_skip;
    int hexadd;

    size_t skip = 0;
    char c;

    while(1)
    {
        last = __string_buffer_last(buffer);

        c = str[skip];
        if(!c)
        {
            break;
        }
        else if(c == '\\')
        {
            ++skip;
            c = str[skip];

            if(c == '\\')
                ;
            else if(c == 'n')
                c = '\n';
            else if(c == 't')
                c = '\t';
            else if(c == 'r')
                c = '\r';
            else if(c == 'n')
                c = '\n';

            last->buffer[last->size] = c;
            ++last->size;
            ++skip;
        }
        else if(c == '%')
        {
            ++skip;
            c = str[skip];

            if(c == '%')
            {
                last->buffer[last->size] = c;
                ++last->size;
                ++skip;
                continue;
            }
            else if(c == 'c')
            {
                c = (char)va_arg(ap, int);
                last->buffer[last->size] = c;
                ++last->size;
                ++skip;
                continue;
            }

            is_zero = false;
            if(c == '0')
            {
                is_zero = true;
                ++skip;
                c = str[skip];
            }

            length = 0;
            while(c >= '0' && c <= '9')
            {
                length *= 10;
                length += c - '0';
                ++skip;
                c = str[skip];
            }

            number_type = 0;
            while(c == 'l')
            {
                ++number_type;
                ++skip;
                c = str[skip];
            }

            if(c == 's')
            {
                ++skip;
                const char *arg = va_arg(ap, const char *);
                // TODO: is_space
                string_buffer_addlstring(buffer, arg, length);
            }
            else if(c == 'd' || c == 'i')
            {
                ++skip;

                radix = 10;
                is_sign = true;
                hexadd = 0;

                goto __string_buffer_addfstring_write_number;
            }
            else if(c == 'u')
            {
                ++skip;

                radix = 10;
                is_sign = false;
                hexadd = 0;

                goto __string_buffer_addfstring_write_number;
            }
            else if(c == 'x')
            {
                ++skip;

                radix = 16;
                is_sign = false;
                hexadd = 'a' - '9' - 1;

                goto __string_buffer_addfstring_write_number;
            }
            else if(c == 'X')
            {
                ++skip;

                radix = 16;
                is_sign = false;
                hexadd = 'A' - '9' - 1;

                goto __string_buffer_addfstring_write_number;
            }
        }
        else
        {
            last->buffer[last->size] = c;
            ++last->size;
            ++skip;
        }

        continue;

__string_buffer_addfstring_write_number:

        if(is_sign)
        {
            int64_t val;

            if(number_type == 0)
                val = (int64_t)va_arg(ap, int);
            else if(number_type == 1)
                val = (int64_t)va_arg(ap, long);
            else
                val = (int64_t)va_arg(ap, int64_t);

            if(val < 0)
                val = 0 - val;
            else
                is_sign = false;

            number = (uint64_t)val;
        }
        else
        {
            if(number_type == 0)
                number = (uint64_t)va_arg(ap, unsigned int);
            else if(number_type == 1)
                number = (uint64_t)va_arg(ap, unsigned long);
            else
                number = (uint64_t)va_arg(ap, uint64_t);
        }

        if(number == 0)
            is_sign = false;

        number_skip = sizeof(number_str);
        do
        {
            --number_skip;
            c = '0' + (number % radix);
            if(c > '9')
                c += hexadd;
            number_str[number_skip] = c;
            number /= radix;
        } while(number != 0);

        if(length > sizeof(number_str))
            length = 0;
        length = sizeof(number_str) - length;

        if(is_zero)
        {
            while(length < number_skip)
            {
                --number_skip;
                number_str[number_skip] = '0';
            }
            if(is_sign)
                number_str[number_skip] = '-';
        }
        else
        {
            if(is_sign)
            {
                --number_skip;
                number_str[number_skip] = '-';
            }
            while(length < number_skip)
            {
                --number_skip;
                number_str[number_skip] = ' ';
            }
        }

        string_buffer_addlstring(  buffer
                                 , &number_str[number_skip]
                                 , sizeof(number_str) - number_skip);
        continue;

    }
}

void string_buffer_addfstring(string_buffer_t *buffer, const char *str, ...)
{
    va_list ap;
    va_start(ap, str);
    strung_buffer_addvastring(buffer, str, ap);
    va_end(ap);
}

char * string_buffer_release(string_buffer_t *buffer, size_t *size)
{
    size_t skip;
    string_buffer_t *next, *next_next;

    for(  skip = 0, next = buffer
        ; next
        ; next = next->next)
    {
        skip += next->size;
    }


    char *str = (char *)malloc(skip + 1);
    for(  skip = 0, next = buffer
        ; next && (next_next = next->next, 1)
        ; next = next_next)
    {
        memcpy(&str[skip], next->buffer, next->size);
        skip += next->size;
        free(next);
    }
    str[skip] = 0;

    if(size)
        *size = skip;

    return str;
}

#ifdef WITH_LUA
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
#endif /* WITH_LUA */

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
