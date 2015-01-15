#ifndef _PARSER_H_
#define _PARSER_H_ 1

#include <astra.h>

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

char * http_authorization(  const char *auth_header
                          , const char *method, const char *path
                          , const char *login, const char *password);

#endif /* _PARSER_H_ */
