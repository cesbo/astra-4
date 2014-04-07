/*
 * Astra Module: HTTP
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
                return false;                                               \
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
        return false;                                                       \
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
        return false;                                                       \
    }                                                                       \
}

/* RFC 3986 */

#define IS_UPPER_ALPHA(_c) (_c >= 'A' && _c <= 'Z')
#define IS_LOWER_ALPHA(_c) (_c >= 'a' && _c <= 'z')
#define IS_ALPHA(_c) (IS_UPPER_ALPHA(_c) || IS_LOWER_ALPHA(_c))

#define IS_DIGIT(_c) (_c >= '0' && _c <= '9')

#define IS_UNRESERVED(_c) (   IS_ALPHA(_c)                                  \
                           || IS_DIGIT(_c)                                  \
                           || (_c == '-')                                   \
                           || (_c == '.')                                   \
                           || (_c == '_')                                   \
                           || (_c == '~'))

#define IS_HEXDIGIT(_c) (   IS_DIGIT(_c)                                    \
                         || (_c >= 'A' && _c <= 'F')                        \
                         || (_c >= 'a' && _c <= 'f'))

#define IS_GEN_DELIMS(_c) (   (_c == ':')                                   \
                           || (_c == '/')                                   \
                           || (_c == '?')                                   \
                           || (_c == '#')                                   \
                           || (_c == '[')                                   \
                           || (_c == ']')                                   \
                           || (_c == '@'))

#define IS_SUB_DELIMS(_c) (   (_c == '!')                                   \
                           || (_c == '$')                                   \
                           || (_c >= '&' && _c <= ',') /* &'()*+, */        \
                           || (_c == ';')                                   \
                           || (_c == '='))

#define IS_RESERVED(_c) (IS_GEN_DELIMS(_c) || IS_SUB_DELIMS(_c))

/* RFC 2616 */


#define IS_CTL(_c) (_c <= 0x1F || _c == 0x7F)

#define IS_SEP(_c) (   (_c == 0x09)                                 \
                    || (_c == ' ')                                  \
                    || (_c == '"')                                  \
                    || (_c == '(')                                  \
                    || (_c == ')')                                  \
                    || (_c == ',')                                  \
                    || (_c == '/')                                  \
                    || (_c == ':')                                  \
                    || (_c == ';')                                  \
                    || (_c == '<')                                  \
                    || (_c == '=')                                  \
                    || (_c == '>')                                  \
                    || (_c == '?')                                  \
                    || (_c == '@')                                  \
                    || (_c == '[')                                  \
                    || (_c == '\\')                                 \
                    || (_c == ']')                                  \
                    || (_c == '{')                                  \
                    || (_c == '}'))

#define IS_TOKEN(_c) (!IS_CTL(_c) && !IS_SEP(_c))

bool http_parse_response(const char *str, parse_match_t *match)
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
    return true;
}

bool http_parse_request(const char *str, parse_match_t *match)
{
    size_t i = 0;
    match[0].so = 0;

    // metod
    SEEK(1, 1, 16, (   IS_UPPER_ALPHA(c)
                    || c == '_'))
    CHECK_SP()

    // uri
    SEEK(2, 1, 1024, (c > 0x20 && c <= 0x7E))
    CHECK_SP()

    // version
    SEEK(3, 1, 16, (   IS_UPPER_ALPHA(c)
                    || IS_DIGIT(c)
                    || (c == '.')
                    || (c == '/')))
    CHECK_CRLF()

    match[0].eo = i;
    return true;
}

bool http_parse_header(const char *str, parse_match_t *match)
{
    size_t skip = 0;

    match[0].so = 0;

    if(str[0] == '\r' && str[1] == '\n')
    {
        // empty line
        match[1].so = 0;
        match[1].eo = 0;
        match[2].so = 0;
        match[2].eo = 0;
        match[0].eo = 2;
        return true;
    }

    // parse key
    match[1].so = 0;
    while(1)
    {
        const char c = str[skip];
        if(skip - match[1].so > 1024)
            return false;

        if(IS_TOKEN(c))
        {
            skip += 1;
        }
        else if(c == ':')
        {
            match[1].eo = skip;

            if(skip - match[1].so == 0)
                return false;

            break;
        }
        else
            return false;
    }

    ++skip; // skip ':'

    if(str[skip] == '\r' && str[skip + 1] == '\n')
    {
        match[2].so = skip;
        match[2].eo = skip;
        match[0].eo = skip + 2;
        return true;
    }

    if(str[skip] != ' ')
        return false;
    ++skip; // skip ' '

    // parse value
    match[2].so = skip;
    while(1)
    {
        if(skip - match[2].so > 1024)
            return false;

        if(str[skip] == '\r' && str[skip + 1] == '\n')
        {
            match[2].eo = skip;
            break;
        }

        skip += 1;
    }

    if(!(str[skip] == '\r' && str[skip + 1] == '\n'))
        return false;

    match[0].eo = skip + 2;
    return true;
}

bool http_parse_chunk(const char *str, parse_match_t *match)
{
    size_t i = 0;
    match[0].so = 0;

    SEEK(1, 1, 8, IS_HEXDIGIT(c))
    if(str[i] == ';')
    {
        // chunk extension
        ++i;
        SEEK(2, 0, 1024, (c >= 0x20 && c <= 0x7E))
    }
    CHECK_CRLF()

    match[0].eo = i;
    return true;
}

bool http_parse_query(const char *str, parse_match_t *match)
{
    size_t skip = 0;

    match[0].so = 0;

    // parse key
    match[1].so = 0;
    while(1)
    {
        const char c = str[skip];
        if(skip - match[1].so > 1024)
            return false;

        if(IS_UNRESERVED(c) || c == '+')
        {
            skip += 1;
        }
        else if(c == '%')
        {
            const char d1 = str[skip + 1];
            if(!IS_HEXDIGIT(d1))
                return false;

            const char d2 = str[skip + 2];
            if(!IS_HEXDIGIT(d2))
                return false;

            skip += 3;
        }
        else
        {
            match[1].eo = skip;

            if(skip - match[1].so == 0)
            {
                match[2].so = skip;
                match[2].eo = skip;
                match[0].eo = skip;
                return true;
            }

            break;
        }
    }

    if(str[skip] != '=')
    {
        match[2].so = skip;
        match[2].eo = skip;

        match[0].eo = skip;
        return true;
    }

    ++skip; // skip '='

    match[2].so = skip;
    while(1)
    {
        const char c = str[skip];
        if(skip - match[2].so > 1024)
            return false;

        if(IS_UNRESERVED(c) || c == '+' || c == '=')
        {
            skip += 1;
        }
        else if(c == '%')
        {
            const char d1 = str[skip + 1];
            if(!IS_HEXDIGIT(d1))
                return false;

            const char d2 = str[skip + 2];
            if(!IS_HEXDIGIT(d2))
                return false;

            skip += 3;
        }
        else
        {
            match[2].eo = skip;
            break;
        }
    }

    match[0].eo = skip;
    return true;
}
