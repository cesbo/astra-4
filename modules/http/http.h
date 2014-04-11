
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

    const char *method;
    const char *path;

    bool is_content_length;
    string_buffer_t *content;

    // response
    event_callback_t on_send;
    event_callback_t on_read;
    event_callback_t on_ready;
    http_response_t *response;
};

void http_response_code(http_client_t *client, int code, const char *message);
void http_response_header(http_client_t *client, const char *header, ...);
void http_response_send(http_client_t *client);

void http_client_warning(http_client_t *client, const char *message, ...);
void http_client_error(http_client_t *client, const char *message, ...);
void http_client_close(http_client_t *client);

void http_client_abort(http_client_t *client, int code, const char *text);

#endif /* _HTTP_H_ */
