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

#define IS_CTL(_c) (_c <= 0x1F || _c == 0x7F)

#define IS_SEP(_c) (   (_c == '\t')                                 \
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
        c = str[skip];
        if(IS_UPPER_ALPHA(c))
            ++skip;
        else if(c == '/')
        {
            ++skip;
            break;
        }
        else
            return false;
    }
    while(1)
    {
        c = str[skip];
        if(IS_DIGIT(c) || (c == '.'))
            ++skip;
        else if(c == ' ')
        {
            match[1].eo = skip;
            ++skip;
            break;
        }
        else
            return false;
    }

    // parse code
    match[2].so = skip;
    while(1)
    {
        c = str[skip];
        if(IS_DIGIT(c))
            ++skip;
        else if(c == ' ')
        {
            if(skip - match[2].so != 3)
                return false;

            match[2].eo = skip;
            ++skip;
            break;
        }
        else
            return false;
    }

    // parse message
    match[3].so = skip;
    while(1)
    {
        c = str[skip];
        if((c >= 0x20 && c < 0x7F) || (c == '\t'))
            ++skip;
        else if(c == '\n')
        {
            match[3].eo = skip;
            skip += 1;
            break;
        }
        else if(c == '\r' && str[skip + 1] == '\n')
        {
            match[3].eo = skip;
            skip += 2;
            break;
        }
        else
            return false;
    }

    match[0].eo = skip;
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
        c = str[skip];

        if(IS_TOKEN(c))
            ++skip;
        else if(c == ' ')
        {
            match[1].eo = skip;
            ++skip;

            if(skip == 0)
                return false;

            break;
        }
        else
            return false;
    }

    // parse path
    match[2].so = skip;
    while(1)
    {
        c = str[skip];

        if(c == ' ')
        {
            match[2].eo = skip;
            ++skip;

            break;
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
        else if(   (c > 0x20 && c < 0x7F)
                && (c != '<')
                && (c != '>')
                && (c != '#')
                && (c != '"'))
        {
            ++skip;
        }
        else
            return false;
    }

    // parse version
    match[3].so = skip;
    while(1)
    {
        c = str[skip];
        if(IS_UPPER_ALPHA(c))
            ++skip;
        else if(c == '/')
        {
            ++skip;
            break;
        }
        else
            return false;
    }
    while(1)
    {
        c = str[skip];
        if(IS_DIGIT(c) || (c == '.'))
            ++skip;
        else if(c == '\n')
        {
            match[3].eo = skip;
            skip += 1;
            break;
        }
        else if(c == '\r' && str[skip + 1] == '\n')
        {
            match[3].eo = skip;
            skip += 2;
            break;
        }
        else
            return false;
    }

    match[0].eo = skip;
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

    // empty line
    if(str[0] == '\n')
    {
        memset(match, 0, sizeof(parse_match_t) * 3);
        match[0].eo = 1;
        return true;
    }
    else if(str[0] == '\r' && str[1] == '\n')
    {
        memset(match, 0, sizeof(parse_match_t) * 3);
        match[0].eo = 2;
        return true;
    }

    // parse key
    match[1].so = 0;
    while(1)
    {
        c = str[skip];

        if(IS_TOKEN(c))
            skip += 1;
        else if(c == ':')
        {
            match[1].eo = skip;

            if(skip == 0)
                return false;

            break;
        }
        else
            return false;
    }

    ++skip; // skip ':'

    c = str[skip];

    if(c == ' ')
        ++skip; // skip ' '
    else if(c == '\n')
    {
        match[2].so = skip;
        match[2].eo = skip;
        match[0].eo = skip + 1;
        return true;
    }
    else if(c == '\r' && str[skip + 1] == '\n')
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
        c = str[skip];

        if(c >= 0x20 && c < 0x7F)
            ++skip;
        else if(c == '\n')
        {
            match[2].eo = skip;
            skip += 1;
            break;
        }
        else if(c == '\r' && str[skip + 1] == '\n')
        {
            match[2].eo = skip;
            skip += 2;
            break;
        }
        else
            return false;
    }

    match[0].eo = skip;
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
        c = str[skip];
        if(IS_HEXDIGIT(c))
            ++skip;
        else
        {
            if(skip == 0)
                return false;

            match[1].eo = skip;

            if(c == '\r' && str[skip + 1] == '\n')
            {
                match[0].eo = skip + 2;
                return true;
            }

            if(c == ';')
                break;

            return false;
        }
    }

    // chunk extension
    ++skip; // skip ';'
    while(1)
    {
        c = str[skip];
        if(IS_TOKEN(c))
            ++skip;
        else if(c == '\n')
        {
            match[0].eo = skip + 1;
            return true;
        }
        else if(c == '\r' && str[skip + 1] == '\n')
        {
            match[0].eo = skip + 2;
            return true;
        }
        else
            break;
    }

    return false;
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
