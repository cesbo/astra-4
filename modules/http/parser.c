/*
 * Astra Module: HTTP
 * http://cesbo.com/en/astra
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

#include <stdio.h>
#include "parser.h"

#define SEEK(_m, _min_l, _max_l, _check_char)                               \
{                                                                           \
    int __l##_m = 0;                                                        \
    match[_m].so = i;                                                       \
    while(1)                                                                \
    {                                                                       \
        const char c = str[i];                                              \
        if(!(_check_char))                                                  \
        {                                                                   \
            if((c == ' ' || c == '\r' || c == '\n')                         \
               && __l##_m >= _min_l                                         \
               && __l##_m <= _max_l)                                        \
            {                                                               \
                match[_m].eo = i;                                           \
                break;                                                      \
            }                                                               \
            else                                                            \
            {                                                               \
                match[0].eo = i;                                            \
                return 0;                                                   \
            }                                                               \
        }                                                                   \
        ++i;                                                                \
        ++__l##_m;                                                          \
    }                                                                       \
}

#define CHECK_SP()                                                          \
{                                                                           \
    if(str[i] == ' ')                                                       \
        ++i;                                                                \
    else                                                                    \
    {                                                                       \
        match[0].eo = i;                                                    \
        return 0;                                                           \
    }                                                                       \
}

#define CHECK_CRLF()                                                        \
{                                                                           \
    if(str[i] == '\r')                                                      \
        ++i;                                                                \
    if(str[i] == '\n')                                                      \
        ++i;                                                                \
    else                                                                    \
    {                                                                       \
        match[0].eo = i;                                                    \
        return 0;                                                           \
    }                                                                       \
}

int http_parse_response(const char *str, parse_match_t *match)
{
    size_t i = 0;
    match[0].so = 0;

    // version
    SEEK(1, 1, 16, (   (c >= 'A' && c <= 'Z')
                    || (c >= '0' && c <= '9')
                    || (c == '.')
                    || (c == '/')))
    CHECK_SP()

    // code
    SEEK(2, 1, 3, (c >= '0' && c <= '9'))
    if(str[i] == ' ')
        CHECK_SP()
    else
        CHECK_CRLF()

    // message
    SEEK(3, 1, 1024, (c >= 0x20 && c <= 0x7E))
    CHECK_CRLF()

    match[0].eo = i;
    return 1;
}

int http_parse_request(const char *str, parse_match_t *match)
{
    size_t i = 0;
    match[0].so = 0;

    // metod
    SEEK(1, 1, 16, ((c >= 'A' && c <= 'Z') || c == '_'))
    CHECK_SP()

    // uri
    SEEK(2, 1, 1024, (c > 0x20 && c <= 0x7E))
    CHECK_SP()

    // version
    SEEK(3, 1, 16, (   (c >= 'A' && c <= 'Z')
                    || (c >= '0' && c <= '9')
                    || (c == '.')
                    || (c == '/')))
    CHECK_CRLF()

    match[0].eo = i;
    return 1;
}

int http_parse_header(const char *str, parse_match_t *match)
{
    size_t i = 0;
    match[0].so = 0;

    SEEK(1, 0, 1024, (c >= 0x20 && c <= 0x7E))
    CHECK_CRLF()

    match[0].eo = i;
    return 1;
}

int http_parse_chunk(const char *str, parse_match_t *match)
{
    size_t i = 0;
    match[0].so = 0;

    SEEK(1, 1, 8, (   (c >= 'A' && c <= 'F')
                   || (c >= 'a' && c <= 'f')
                   || (c >= '0' && c <= '9')))
    if(str[i] == ';')
    {
        // chunk extension
        ++i;
        SEEK(2, 0, 1024, (c >= 0x20 && c <= 0x7E))
    }
    CHECK_CRLF()

    match[0].eo = i;
    return 1;
}
