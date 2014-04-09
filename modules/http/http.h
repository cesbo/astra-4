
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

    bool is_content_length;
    string_buffer_t *content;

    // response
    http_response_t *response;
} http_client_t;

void http_client_close(http_client_t *client);

#endif /* _HTTP_H_ */
