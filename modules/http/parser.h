/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#ifndef _PARSE_H_
#define _PARSE_H_

#include <stddef.h>

typedef struct
{
    size_t so;
    size_t eo;
} parse_match_t;

int http_parse_request(const char *, parse_match_t *);
int http_parse_response(const char *, parse_match_t *);
int http_parse_header(const char *, parse_match_t *);
int http_parse_chunk(const char *, parse_match_t *);

#endif /* _PARSE_H_ */
