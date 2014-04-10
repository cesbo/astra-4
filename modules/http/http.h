
#ifndef _HTTP_H_
#define _HTTP_H_ 1

#include <astra.h>

#define HTTP_BUFFER_SIZE (16 * 1024)

typedef struct http_response_t http_response_t;

typedef struct
{
    MODULE_STREAM_DATA();

    module_data_t *mod;

    int idx_data;

    asc_socket_t *sock;

    char buffer[HTTP_BUFFER_SIZE];
    size_t buffer_skip;
    size_t chunk_left;

    // request
    int status; // 1 - empty line is found, 2 - request ready, 3 - release
    int idx_request;
    int idx_callback;

    const char *method;
    const char *path;

    bool is_content_length;
    string_buffer_t *content;

    // response
    socket_callback_t on_ready;
    http_response_t *response;
} http_client_t;

void http_response_code(http_client_t *client, int code, const char *message);
void http_response_header(http_client_t *client, const char *header, ...);
void http_response_send(http_client_t *client);

void http_client_warning(http_client_t *client, const char *message, ...);
void http_client_error(http_client_t *client, const char *message, ...);
void http_client_close(http_client_t *client);

void http_client_abort(http_client_t *client, int code, const char *text);

#endif /* _HTTP_H_ */
