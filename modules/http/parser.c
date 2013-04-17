/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
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
