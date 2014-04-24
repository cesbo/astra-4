/*
 * Astra Module: HTTP
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

#include "http.h"

void lua_string_to_lower(const char *str, size_t size)
{
    if(size == 0)
    {
        lua_pushstring(lua, "");
        return;
    }

    size_t skip = 0;
    string_buffer_t *buffer = string_buffer_alloc();
    while(skip < size)
    {
        const char c = str[skip];
        if(c >= 'A' && c <= 'Z')
            string_buffer_addchar(buffer, c + ('a' - 'A'));
        else
            string_buffer_addchar(buffer, c);

        skip += 1;
    }
    string_buffer_push(lua, buffer);
}

void lua_url_decode(const char *str, size_t size)
{
    if(size == 0)
    {
        lua_pushstring(lua, "");
        return;
    }

    size_t skip = 0;
    string_buffer_t *buffer = string_buffer_alloc();
    while(skip < size)
    {
        const char c = str[skip];
        if(c == '%')
        {
            char c = ' ';
            str_to_hex(&str[skip + 1] , (uint8_t *)&c, 1);
            string_buffer_addchar(buffer, c);
            skip += 3;
        }
        else if(c == '+')
        {
            string_buffer_addchar(buffer, ' ');
            skip += 1;
        }
        else
        {
            string_buffer_addchar(buffer, c);
            skip += 1;
        }
    }
    string_buffer_push(lua, buffer);
}

bool lua_parse_query(const char *str, size_t size)
{
    size_t skip = 0;
    parse_match_t m[3];

    lua_newtable(lua);
    while(skip < size && http_parse_query(&str[skip], m))
    {
        if(m[1].eo > m[1].so)
        {
            lua_url_decode(&str[skip + m[1].so], m[1].eo - m[1].so); // key
            lua_url_decode(&str[skip + m[2].so], m[2].eo - m[2].so); // value

            lua_settable(lua, -3);
        }

        skip += m[0].eo;

        if(skip < size)
            ++skip; // skip &
    }

    return (skip == size);
}

bool lua_safe_path(const char *str, size_t size)
{
    size_t skip = 0;

    size_t sskip = 0;
    char *safe = malloc(size + 1);

    while(skip < size)
    {
        if(   (str[skip] == '/')
           && (str[skip + 1] == '.' && str[skip + 2] == '.')
           && (str[skip + 3] == '/' || skip + 3 == size))
        {
            while(sskip > 0)
            {
                -- sskip;
                if(safe[sskip] == '/')
                    break;
            }
            skip += 3;
            if(skip == size)
            {
                safe[sskip] = '/';
                ++ sskip;
            }
        }
        else
        {
            safe[sskip] = str[skip];
            ++ sskip;
            ++ skip;
        }
    }

    lua_pushlstring(lua, safe, sskip);
    free(safe);

    return (skip == sskip);
}
