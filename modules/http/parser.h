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

#ifndef _PARSE_H_
#define _PARSE_H_

#include <stddef.h>
#include <stdbool.h>

typedef struct
{
    size_t so;
    size_t eo;
} parse_match_t;

bool http_parse_request(const char *, parse_match_t *);
bool http_parse_response(const char *, parse_match_t *);
bool http_parse_header(const char *, parse_match_t *);
bool http_parse_chunk(const char *, parse_match_t *);
bool http_parse_query(const char *, parse_match_t *);

#endif /* _PARSE_H_ */
