/*
 * Astra Module: HTTP
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2015, Andrey Dyldin <and@cesbo.com>
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

#ifndef _PARSER_H_
#define _PARSER_H_ 1

#include <astra.h>

typedef struct
{
    size_t so;
    size_t eo;
} parse_match_t;

bool parse_skip_word(const char *str, size_t size, size_t *skip);
bool parse_skip_space(const char *str, size_t size, size_t *skip);
bool parse_skip_line(const char *str, size_t size, size_t *skip);
#define parse_get_line_size(_str, _skip)                                                        \
    (((_skip >= 2) && (_str[_skip - 2] == '\r')) ? (_skip - 2) : (_skip - 1))

bool http_parse_request(const char *, size_t size, parse_match_t *);
bool http_parse_response(const char *, size_t size, parse_match_t *);
bool http_parse_header(const char *, size_t size, parse_match_t *);
bool http_parse_chunk(const char *, size_t size, parse_match_t *);
bool http_parse_query(const char *, size_t size, parse_match_t *);

char * http_authorization(const char *auth_header, size_t size,
    const char *method, const char *path,
    const char *login, const char *password);

#endif /* _PARSER_H_ */
