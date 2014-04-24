
#ifndef _HTTP_H_
#define _HTTP_H_ 1

#include <astra.h>

#define HTTP_BUFFER_SIZE (16 * 1024)

typedef struct http_response_t http_response_t;
typedef struct http_client_t http_client_t;

struct http_client_t
{
    module_data_t *mod; // http_server module

    int idx_server;     // http_server instance (mod->idx_self)
    int idx_data;

    asc_socket_t *sock;

    char buffer[HTTP_BUFFER_SIZE];
    size_t buffer_skip;
    size_t chunk_left;

    // request
    int status;         // 1 - empty line is found, 2 - request ready, 3 - release
    int idx_request;
    int idx_callback;   // route callback

    bool is_head;
    bool is_content_length;
    string_buffer_t *content;

    // response
    event_callback_t on_send;
    event_callback_t on_read;
    event_callback_t on_ready;
    http_response_t *response;

    int idx_content;
};

// HTTP Server API

void http_response_code(http_client_t *client, int code, const char *message);
void http_response_header(http_client_t *client, const char *header, ...);
void http_response_send(http_client_t *client);

void http_client_warning(http_client_t *client, const char *message, ...);
void http_client_error(http_client_t *client, const char *message, ...);
void http_client_close(http_client_t *client);

void http_client_redirect(http_client_t *client, int code, const char *location);
void http_client_abort(http_client_t *client, int code, const char *text);

// Utils

void lua_string_to_lower(const char *str, size_t size);
void lua_url_decode(const char *str, size_t size);
bool lua_parse_query(const char *str, size_t size);
bool lua_safe_path(const char *str, size_t size);

// Parser

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

#endif /* _HTTP_H_ */
