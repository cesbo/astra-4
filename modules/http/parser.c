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

/* RFC: 2616 (HTTP/1.1), 3986 (URI) */

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

/*
 * oooooooooo  ooooooooooo  oooooooo8 oooooooooo
 *  888    888  888    88  888         888    888
 *  888oooo88   888ooo8     888oooooo  888oooo88
 *  888  88o    888    oo          888 888         ooo
 * o888o  88o8 o888ooo8888 o88oooo888 o888o        888
 *
 */

bool http_parse_response(const char *str, parse_match_t *match)
{
    char c;
    size_t skip = 0;
    match[0].so = 0;

    // parse version
    match[1].so = skip;
    while(1)
    {
        if(skip >= match[0].eo)
            return false;

        c = str[skip];
        if(c == ' ' || c == '\t')
            break;
        else
            ++skip;
    }
    match[1].eo = skip;

    // skip space
    while(1)
    {
        if(skip >= match[0].eo)
            return false;

        c = str[skip];
        if(c == ' ' || c == '\t')
            ++skip;
        else
            break;
    }

    // parse code
    match[2].so = skip;
    while(1)
    {
        if(skip >= match[0].eo)
            return false;

        c = str[skip];
        if(c == ' ' || c == '\t')
            break;
        else
            ++skip;
    }
    match[2].eo = skip;

    // skip space
    while(1)
    {
        if(skip >= match[0].eo)
            return false;

        c = str[skip];
        if(c == ' ' || c == '\t')
            ++skip;
        else
            break;
    }

    // parse message
    match[3].so = skip;
    while(1)
    {
        if(skip + 1 >= match[0].eo)
            return false;

        if(str[skip] == '\r' && str[skip + 1] == '\n')
            break;
        else
            ++skip;
    }
    match[3].eo = skip;

    match[0].eo = skip + 2;
    return true;
}

/*
 * oooooooooo  ooooooooooo  ooooooo
 *  888    888  888    88 o888   888o
 *  888oooo88   888ooo8   888     888
 *  888  88o    888    oo 888o  8o888  ooo
 * o888o  88o8 o888ooo8888  88ooo88    888
 *                               88o8
 */

bool http_parse_request(const char *str, parse_match_t *match)
{
    char c;
    size_t skip = 0;
    match[0].so = 0;

    // parse method
    match[1].so = 0;
    while(1)
    {
        if(skip >= match[0].eo)
            return false;

        c = str[skip];
        if(c == ' ' || c == '\t')
            break;
        else
            ++skip;
    }
    match[1].eo = skip;

    // skip space
    while(1)
    {
        if(skip >= match[0].eo)
            return false;

        c = str[skip];
        if(c == ' ' || c == '\t')
            ++skip;
        else
            break;
    }

    // parse path
    match[2].so = skip;
    while(1)
    {
        if(skip >= match[0].eo)
            return false;

        c = str[skip];
        if(c == ' ' || c == '\t')
            break;
        else
            ++skip;
    }
    match[2].eo = skip;

    // skip space
    while(1)
    {
        if(skip >= match[0].eo)
            return false;

        c = str[skip];
        if(c == ' ' || c == '\t')
            ++skip;
        else
            break;
    }

    // parse version
    match[3].so = skip;
    while(1)
    {
        if(skip + 1 >= match[0].eo)
            return false;

        if(str[skip] == '\r' && str[skip + 1] == '\n')
            break;
        else
            ++skip;
    }
    match[3].eo = skip;

    match[0].eo = skip + 2;
    return true;
}

/*
 * ooooo ooooo ooooooooooo      o      ooooooooo  ooooooooooo oooooooooo
 *  888   888   888    88      888      888    88o 888    88   888    888
 *  888ooo888   888ooo8       8  88     888    888 888ooo8     888oooo88
 *  888   888   888    oo    8oooo88    888    888 888    oo   888  88o
 * o888o o888o o888ooo8888 o88o  o888o o888ooo88  o888ooo8888 o888o  88o8
 *
 */

bool http_parse_header(const char *str, parse_match_t *match)
{
    char c;
    size_t skip = 0;
    match[0].so = 0;

    if(match[0].eo < 2)
        return false;

    // empty line
    if(str[0] == '\r' && str[1] == '\n')
    {
        match[1].so = 0;
        match[1].eo = 0;
        match[0].eo = 2;
        return true;
    }

    // parse key
    match[1].so = 0;
    while(1)
    {
        if(skip >= match[0].eo)
            return false;

        if(str[skip] == ':')
            break;
        else
            ++skip;
    }
    match[1].eo = skip;

    // skip ':'
    ++skip;

    // skip space
    while(1)
    {
        if(skip >= match[0].eo)
            return false;

        c = str[skip];
        if(c == ' ' || c == '\t')
            ++skip;
        else
            break;
    }

    // check eol
    if(skip + 2 >= match[0].eo)
        return false;

    if(str[skip] == '\r' && str[skip + 1] == '\n')
    {
        match[2].so = skip;
        match[2].eo = skip;
        match[0].eo = skip + 2;
        return true;
    }

    // parse value
    match[2].so = skip;
    while(1)
    {
        if(skip + 1 >= match[0].eo)
            return false;

        if(str[skip] == '\r' && str[skip + 1] == '\n')
            break;
        else
            ++skip;
    }
    match[2].eo = skip;

    match[0].eo = skip + 2;
    return true;
}

/*
 *   oooooooo8 ooooo ooooo ooooo  oooo oooo   oooo oooo   oooo
 * o888     88  888   888   888    88   8888o  88   888  o88
 * 888          888ooo888   888    88   88 888o88   888888
 * 888o     oo  888   888   888    88   88   8888   888  88o
 *  888oooo88  o888o o888o   888oo88   o88o    88  o888o o888o
 *
 */

bool http_parse_chunk(const char *str, parse_match_t *match)
{
    char c;
    size_t skip = 0;
    match[0].so = 0;

    // parse chunk
    match[1].so = 0;
    while(1)
    {
        if(skip + 1 >= match[0].eo)
            return false;

        c = str[skip];

        if((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))
            ++skip;
        else if(c == '\r' && str[skip + 1] == '\n')
        {
            match[1].eo = skip;
            match[0].eo = skip + 2;
            return true;
        }
        else if(c == ';')
            break;
        else
            return false;
    }

    // chunk extension
    ++skip; // skip ';'
    while(1)
    {
        if(skip + 1 >= match[0].eo)
            return false;

        if(str[skip] == '\r' && str[skip + 1] == '\n')
            break;
        else
            ++skip;
    }
    match[0].eo = skip + 2;

    return true;
}

/*
 *   ooooooo  ooooo  oooo ooooooooooo oooooooooo ooooo  oooo
 * o888   888o 888    88   888    88   888    888  888  88
 * 888     888 888    88   888ooo8     888oooo88     888
 * 888o  8o888 888    88   888    oo   888  88o      888
 *   88ooo88    888oo88   o888ooo8888 o888o  88o8   o888o
 *        88o8
 */

bool http_parse_query(const char *str, parse_match_t *match)
{
    char c;
    size_t skip = 0;
    match[0].so = 0;

    // parse key
    match[1].so = 0;
    while(1)
    {
        c = str[skip];

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

            if(skip == 0)
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
        c = str[skip];

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
