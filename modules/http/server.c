/*
 * Astra Module: HTTP Server
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

/*
 * Module Name:
 *      http_server
 *
 * Module Options:
 *      addr        - string, server IP address
 *      port        - number, server port
 *      server_name - string, default value: "Astra"
 *      callback    - function,
 *
 * Module Methods:
 *      port()      - return number, server port
 *      close()     - close server
 *      close(client)
 *                  - close client connection
 *      send(client, response)
 *                  - response - table, possible values:
 *                    * code - number, response code. required
 *                    * message - string, response code description. default: see http_code()
 *                    * version - string, protocol version. default: "HTTP/1.1"
 *                    * headers - table (list of strings), response headers
 *                    * location - string, Location header for 301, 302
 *                    * content - string, response body from the string
 *                    * file - string, full path to file, reponse body from the file
 *                    * upstream - object, stream instance returned by module_instance:stream()
 *      data(client)
 *                  - return table, client data
 */

#include <astra.h>
#include "parser.h"

#define MSG(_msg) "[http_server %s:%d] " _msg, mod->addr, mod->port
#define HTTP_BUFFER_SIZE (16 * 1024)

/* WebSocket Frame */
#define FRAME_HEADER_SIZE 2
#define FRAME_KEY_SIZE 4
#define FRAME_SIZE8_SIZE 0
#define FRAME_SIZE16_SIZE 2
#define FRAME_SIZE64_SIZE 8

typedef struct
{
    MODULE_STREAM_DATA();

    module_data_t *mod;

    int idx_data;

    asc_socket_t *sock;

    char buffer[HTTP_BUFFER_SIZE];

    int idx_request;
    bool is_request_line;
    bool is_request_headers;
    bool is_request_ok;

    bool is_connection_close;
    bool is_connection_keep_alive;

    bool is_content;
    bool is_content_length;
    bool is_json;
    bool is_urlencoded;
    bool is_multipart;

    bool is_websocket;
    struct
    {
        char *accept_key;

        uint64_t data_size;

        uint8_t frame_key[FRAME_KEY_SIZE];
        uint8_t frame_key_i;
    } websocket;

    size_t chunk_left; // is_content_length

    string_buffer_t *content;

    // response
    size_t response_size;

    int idx_content;
} http_client_t;

struct module_data_t
{
    MODULE_LUA_DATA();

    int idx_self;

    const char *addr;
    int port;
    const char *server_name;

    asc_socket_t *sock;
    asc_list_t *clients;
};

static const char __method[] = "method";
static const char __version[] = "version";
static const char __path[] = "path";
static const char __query[] = "query";
static const char __headers[] = "headers";
static const char __content[] = "content";
static const char __callback[] = "callback";
static const char __code[] = "code";
static const char __message[] = "message";
static const char __filename[] = "filename";

static const char __content_length[] = "Content-Length: ";

static const char __content_type[] = "Content-Type: ";
static const char __multipart[] = "multipart";
// TODO: static const char __boundary[] = "boundary";
static const char __mime_json[] = "application/json";
static const char __mime_urlencoded[] = "application/x-www-form-urlencoded";

static const char __connection[] = "Connection: ";
static const char __close[] = "close";
static const char __keep_alive[] = "keep-alive";

static const char __upgrade[] = "Upgrade: ";
static const char __websocket[] = "websocket";

static const char __sec_websocket_key[] = "Sec-WebSocket-Key: ";
static const char __websocket_magic[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

/*
 *   oooooooo8 ooooo       ooooo ooooooooooo oooo   oooo ooooooooooo
 * o888     88  888         888   888    88   8888o  88  88  888  88
 * 888          888         888   888ooo8     88 888o88      888
 * 888o     oo  888      o  888   888    oo   88   8888      888
 *  888oooo88  o888ooooo88 o888o o888ooo8888 o88o    88     o888o
 *
 */

static void callback(http_client_t *client)
{
    const int request = lua_gettop(lua);
    lua_rawgeti(lua, LUA_REGISTRYINDEX, client->mod->__lua.oref);
    lua_getfield(lua, -1, __callback);
    lua_rawgeti(lua, LUA_REGISTRYINDEX, client->mod->idx_self);
    lua_pushlightuserdata(lua, client);
    lua_pushvalue(lua, request);
    lua_call(lua, 3, 0);
    lua_pop(lua, 2); // oref + request
}

static void on_client_close(void *arg)
{
    http_client_t *client = arg;
    module_data_t *mod = client->mod;

    if(!client->sock)
        return;

    asc_socket_close(client->sock);
    client->sock = NULL;

    if(client->is_request_ok)
    {
        lua_pushnil(lua);
        callback(client);
    }

    if(client->idx_request)
    {
        luaL_unref(lua, LUA_REGISTRYINDEX, client->idx_request);
        client->idx_request = 0;
    }

    if(client->idx_data)
    {
        luaL_unref(lua, LUA_REGISTRYINDEX, client->idx_data);
        client->idx_data = 0;
    }

    if(client->content)
    {
        string_buffer_free(client->content);
        client->content = NULL;
    }

    if(client->websocket.accept_key)
    {
        free(client->websocket.accept_key);
        client->websocket.accept_key = NULL;
    }

    if(client->idx_content)
    {
        luaL_unref(lua, LUA_REGISTRYINDEX, client->idx_content);
        client->idx_content = 0;
    }

    asc_list_remove_item(mod->clients, client);
    free(client);
}

static void lua_url_decode(const char *str, size_t size)
{
    if(size == 0)
    {
        lua_pushstring(lua, "");
        return;
    }

    size_t skip = 0;
    string_buffer_t *buffer = string_buffer_alloc();
    while(skip < size)
    {
        const char c = str[skip];
        if(c == '%')
        {
            char c = ' ';
            str_to_hex(&str[skip + 1] , (uint8_t *)&c, 1);
            string_buffer_addchar(buffer, c);
            skip += 3;
        }
        else if(c == '+')
        {
            string_buffer_addchar(buffer, ' ');
            skip += 1;
        }
        else
        {
            string_buffer_addchar(buffer, c);
            skip += 1;
        }
    }
    string_buffer_push(lua, buffer);
}

static bool lua_parse_query(const char *str, size_t size)
{
    size_t skip = 0;
    parse_match_t m[3];

    lua_newtable(lua);
    while(skip < size && http_parse_query(&str[skip], m))
    {
        if(m[1].eo > m[1].so)
        {
            lua_url_decode(&str[skip + m[1].so], m[1].eo - m[1].so); // key
            lua_url_decode(&str[skip + m[2].so], m[2].eo - m[2].so); // value

            lua_settable(lua, -3);
        }

        skip += m[0].eo;

        if(skip < size)
            ++skip; // skip &
    }

    return (skip == size);
}

/*
 * oooo     oooo oooooooo8          oooooooooo  ooooooooooo      o      ooooooooo
 *  88   88  88 888                  888    888  888    88      888      888    88o
 *   88 888 88   888oooooo  ooooooo  888oooo88   888ooo8       8  88     888    888
 *    888 888           888          888  88o    888    oo    8oooo88    888    888
 *     8   8    o88oooo888          o888o  88o8 o888ooo8888 o88o  o888o o888ooo88
 *
 */

static void on_client_read_websocket(void *arg)
{
    http_client_t *client = arg;
    module_data_t *mod = client->mod;

    ssize_t size = asc_socket_recv(client->sock, client->buffer, HTTP_BUFFER_SIZE);
    if(size <= 0)
    {
        on_client_close(client);
        return;
    }

    uint8_t *data = (uint8_t *)client->buffer;

    if(client->websocket.data_size == 0)
    {
        // TODO: check FIN, OPCODE
        client->websocket.data_size = data[1] & 0x7F;

        if(client->websocket.data_size > 127)
        {
            asc_log_warning(MSG("wrong websocket frame format"));
            client->websocket.data_size = 0;
        }
        else if(client->websocket.data_size < 126)
        {
            data = &data[FRAME_HEADER_SIZE + FRAME_SIZE8_SIZE];
        }
        else if(client->websocket.data_size == 126)
        {
            client->websocket.data_size = (data[2] << 8) | data[3];

            data = &data[FRAME_HEADER_SIZE + FRAME_SIZE16_SIZE];
        }
        else if(client->websocket.data_size == 127)
        {
            client->websocket.data_size = (  ((uint64_t)data[2] << 56)
                                           | ((uint64_t)data[3] << 48)
                                           | ((uint64_t)data[4] << 40)
                                           | ((uint64_t)data[5] << 32)
                                           | ((uint64_t)data[6] << 24)
                                           | ((uint64_t)data[7] << 16)
                                           | ((uint64_t)data[8] << 8 )
                                           | ((uint64_t)data[9]      ));

            data = &data[FRAME_HEADER_SIZE + FRAME_SIZE64_SIZE];
        }

        client->websocket.frame_key_i = 0;
        memcpy(client->websocket.frame_key, data, FRAME_KEY_SIZE);

        data = &data[FRAME_KEY_SIZE];
    }

    // TODO: SSE
    ssize_t skip = 0;
    while(skip < size)
    {
        data[skip] ^= client->websocket.frame_key[client->websocket.frame_key_i % 4];
        ++skip;
        ++client->websocket.frame_key_i;
    }
    client->websocket.data_size -= skip;

    if(!client->content)
    {
        if(client->websocket.data_size == 0)
        {
            lua_rawgeti(lua, LUA_REGISTRYINDEX, client->idx_request);
            lua_pushlstring(lua, (const char *)data, skip);
            lua_setfield(lua, -2, __content);
            callback(client);
        }
        else
        {
            client->content = string_buffer_alloc();
            string_buffer_addlstring(client->content, (const char *)data, skip);
        }
    }
    else
    {
        string_buffer_addlstring(client->content, (const char *)data, skip);

        if(client->websocket.data_size == 0)
        {
            lua_rawgeti(lua, LUA_REGISTRYINDEX, client->idx_request);
            string_buffer_push(lua, client->content);
            client->content = NULL;
            lua_setfield(lua, -2, __content);
            callback(client);
        }
    }
}

/*
 * oooooooooo  ooooooooooo      o      ooooooooo
 *  888    888  888    88      888      888    88o
 *  888oooo88   888ooo8       8  88     888    888
 *  888  88o    888    oo    8oooo88    888    888
 * o888o  88o8 o888ooo8888 o88o  o888o o888ooo88
 *
 */

static void on_client_read(void *arg)
{
    http_client_t *client = arg;
    module_data_t *mod = client->mod;

    ssize_t skip = 0;

    ssize_t size = asc_socket_recv(client->sock, client->buffer, HTTP_BUFFER_SIZE);
    if(size <= 0)
    {
        on_client_close(client);
        return;
    }

    if(client->is_request_ok)
    {
        asc_log_warning(MSG("received data after request"));
        return;
    }

    parse_match_t m[4];

/*
 *     oooooooooo  ooooooooooo  ooooooo  ooooo  oooo ooooooooooo  oooooooo8 ooooooooooo
 *      888    888  888    88 o888   888o 888    88   888    88  888        88  888  88
 *      888oooo88   888ooo8   888     888 888    88   888ooo8     888oooooo     888
 * ooo  888  88o    888    oo 888o  8o888 888    88   888    oo          888    888
 * 888 o888o  88o8 o888ooo8888  88ooo88    888oo88   o888ooo8888 o88oooo888    o888o
 *                                   88o8
 */

    if(!client->is_request_line)
    {
        if(!http_parse_request(client->buffer, m))
        {
            asc_log_error(MSG("failed to parse request line"));
            on_client_close(client);
            return;
        }

        lua_newtable(lua);
        lua_pushvalue(lua, -1);
        client->idx_request = luaL_ref(lua, LUA_REGISTRYINDEX);
        const int request = lua_gettop(lua);

        lua_pushlstring(lua, &client->buffer[m[1].so], m[1].eo - m[1].so);
        lua_setfield(lua, request, __method);
        size_t path_skip = m[2].so;
        while(path_skip < m[2].eo && client->buffer[path_skip] != '?')
            ++path_skip;
        if(path_skip < m[2].eo)
        {
            lua_pushlstring(lua, &client->buffer[m[2].so], path_skip - m[2].so);
            lua_setfield(lua, request, __path);

            ++path_skip; // skip '?'
            if(!lua_parse_query(&client->buffer[path_skip], m[2].eo - path_skip))
            {
                asc_log_error(MSG("failed to parse query line"));
                lua_pop(lua, 2); // query + request
                on_client_close(client);
                return;
            }
            lua_setfield(lua, request, __query);
        }
        else
        {
            lua_pushlstring(lua, &client->buffer[m[2].so], m[2].eo - m[2].so);
            lua_setfield(lua, request, __path);
        }
        lua_pushlstring(lua, &client->buffer[m[3].so], m[3].eo - m[3].so);
        lua_setfield(lua, request, __version);

        skip += m[0].eo;
        client->is_request_line = true;

        lua_pop(lua, 1); // request

        if(skip >= size)
            return;
    }

/*
 *     ooooo ooooo ooooooooooo      o      ooooooooo  ooooooooooo oooooooooo   oooooooo8
 *      888   888   888    88      888      888    88o 888    88   888    888 888
 *      888ooo888   888ooo8       8  88     888    888 888ooo8     888oooo88   888oooooo
 * ooo  888   888   888    oo    8oooo88    888    888 888    oo   888  88o           888
 * 888 o888o o888o o888ooo8888 o88o  o888o o888ooo88  o888ooo8888 o888o  88o8 o88oooo888
 *
 */

    if(!client->is_request_headers)
    {
        lua_rawgeti(lua, LUA_REGISTRYINDEX, client->idx_request);
        const int request = lua_gettop(lua);

        lua_getfield(lua, request, __headers);
        if(lua_isnil(lua, -1))
        {
            lua_pop(lua, 1);
            lua_newtable(lua);
            lua_pushvalue(lua, -1);
            lua_setfield(lua, request, __headers);
        }
        int headers_count = luaL_len(lua, -1);
        const int headers = lua_gettop(lua);

        while(skip < size && http_parse_header(&client->buffer[skip], m))
        {
            const size_t so = m[1].so;
            const size_t length = m[1].eo - so;

            if(!length)
            { /* empty line */
                skip += m[0].eo;
                client->is_request_headers = true;
                break;
            }

            const char *header = &client->buffer[skip + so];
            if(!strncasecmp(header, __connection, sizeof(__connection) - 1))
            {
                const char *val = &header[sizeof(__connection) - 1];
                if(!strncasecmp(val, __close, sizeof(__close) - 1))
                    client->is_connection_close = true;
                else if(!strncasecmp(val, __keep_alive, sizeof(__keep_alive) - 1))
                    client->is_connection_keep_alive = true;
            }
            else if(!strncasecmp(header, __content_length, sizeof(__content_length) - 1))
            {
                const char *val = &header[sizeof(__content_length) - 1];
                client->is_content = true;
                client->is_content_length = true;
                client->chunk_left = strtoul(val, NULL, 10);
            }
            else if(!strncasecmp(header, __content_type, sizeof(__content_type) - 1))
            {
                const char *val = &header[sizeof(__content_type) - 1];
                client->is_content = true;
                if(!strncasecmp(val, __multipart, sizeof(__multipart) - 1))
                {
                    // TODO: multipart
                    client->is_multipart = true;
                }
                else if(!strncasecmp(val, __mime_json, sizeof(__mime_json) - 1))
                    client->is_json = true;
                else if(!strncasecmp(val, __mime_urlencoded, sizeof(__mime_urlencoded) - 1))
                    client->is_urlencoded = true;
            }
            else if(!strncasecmp(header, __upgrade, sizeof(__upgrade) - 1))
            {
                const char *val = &header[sizeof(__upgrade) - 1];
                if(!strncasecmp(val, __websocket, sizeof(__websocket) - 1))
                {
                    client->is_websocket = true;
                    lua_pushboolean(lua, true);
                    lua_setfield(lua, request, __websocket);
                }
            }
            else if(!strncasecmp(header, __sec_websocket_key, sizeof(__sec_websocket_key) - 1))
            {
                sha1_ctx_t ctx;
                memset(&ctx, 0, sizeof(sha1_ctx_t));
                sha1_init(&ctx);
                sha1_update(&ctx
                            , (const uint8_t *)&header[sizeof(__sec_websocket_key) - 1]
                            , length - (sizeof(__sec_websocket_key) - 1));
                sha1_update(&ctx
                            , (const uint8_t *)__websocket_magic
                            , sizeof(__websocket_magic) - 1);
                uint8_t digest[SHA1_DIGEST_SIZE];
                sha1_final(&ctx, digest);

                if(client->websocket.accept_key)
                    free(client->websocket.accept_key);
                client->websocket.accept_key = base64_encode(digest, sizeof(digest), NULL);
            }

            ++headers_count;
            lua_pushnumber(lua, headers_count);
            lua_pushlstring(lua, header, length);
            lua_settable(lua, headers);
            skip += m[0].eo;
        }

        lua_pop(lua, 2); // headers + request

        if(client->is_request_headers)
        {
            if(!client->is_content)
            {
                client->is_request_ok = true;
                lua_rawgeti(lua, LUA_REGISTRYINDEX, client->idx_request);
                callback(client);
                return;
            }
        }

        if(skip >= size)
            return;
    }

/*
 *       oooooooo8   ooooooo  oooo   oooo ooooooooooo ooooooooooo oooo   oooo ooooooooooo
 *     o888     88 o888   888o 8888o  88  88  888  88  888    88   8888o  88  88  888  88
 *     888         888     888 88 888o88      888      888ooo8     88 888o88      888
 * ooo 888o     oo 888o   o888 88   8888      888      888    oo   88   8888      888
 * 888  888oooo88    88ooo88  o88o    88     o888o    o888ooo8888 o88o    88     o888o
 *
 */

    if(!client->content)
        client->content = string_buffer_alloc();

    // Content-Length: *
    if(client->is_content_length)
    {
        const size_t tail = size - skip;

        if(client->chunk_left > tail)
        {
            string_buffer_addlstring(client->content
                                     , &client->buffer[skip]
                                     , tail);
            client->chunk_left -= tail;
        }
        else
        {
            string_buffer_addlstring(client->content
                                     , &client->buffer[skip]
                                     , client->chunk_left);
            client->chunk_left = 0;

            lua_rawgeti(lua, LUA_REGISTRYINDEX, client->idx_request);
            if(client->is_json)
            {
                lua_getglobal(lua, "json");
                lua_getfield(lua, -1, "decode");
                lua_remove(lua, -2); // json
                string_buffer_push(lua, client->content);
                client->content = NULL;
                lua_call(lua, 1, 1);
            }
            else if(client->is_urlencoded)
            {
                string_buffer_push(lua, client->content);
                client->content = NULL;
                const char *str = lua_tostring(lua, -1);
                const int size = luaL_len(lua, -1);
                if(!lua_parse_query(str, size))
                {
                    asc_log_error(MSG("failed to parse urlencoded content"));
                    lua_pop(lua, 1); // query
                }
                else
                {
                    lua_remove(lua, -2); // buffer
                }
            }
            else
            {
                string_buffer_push(lua, client->content);
                client->content = NULL;
            }

            client->is_request_ok = true;
            lua_setfield(lua, -2, __content);
            callback(client);
        }

        return;
    }
}

/*
 *  oooooooo8 ooooooooooo oooo   oooo ooooooooo
 * 888         888    88   8888o  88   888    88o
 *  888oooooo  888ooo8     88 888o88   888    888
 *         888 888    oo   88   8888   888    888
 * o88oooo888 o888ooo8888 o88o    88  o888ooo88
 *
 */

static void on_ready_send_content(void *arg)
{
    http_client_t *client = arg;
    module_data_t *mod = client->mod;

    lua_rawgeti(lua, LUA_REGISTRYINDEX, client->idx_content);
    const size_t content_size = luaL_len(lua, -1);
    const char *content = lua_tostring(lua, -1);

    const size_t content_left = content_size - client->chunk_left;
    const size_t content_send = (content_left > HTTP_BUFFER_SIZE)
                              ? HTTP_BUFFER_SIZE
                              : content_left;

    const ssize_t send_size = asc_socket_send(  client->sock
                                              , (void *)&content[client->chunk_left]
                                              , content_send);
    if(send_size <= 0)
    {
        asc_log_error(MSG("failed to send content to client:%d [%s]")
                      , asc_socket_fd(client->sock), asc_socket_error());
        on_client_close(client);
        return;
    }
    client->chunk_left += send_size;

    if(client->chunk_left == content_size)
    {
        client->chunk_left = 0;

        luaL_unref(lua, LUA_REGISTRYINDEX, client->idx_content);
        client->idx_content = 0;
        asc_socket_set_on_ready(client->sock, NULL);

        if(client->is_connection_close)
            on_client_close(client);
    }
}

static void on_ready_send_response(void *arg)
{
    http_client_t *client = arg;
    module_data_t *mod = client->mod;

    const size_t content_left = client->response_size - client->chunk_left;

    const ssize_t send_size = asc_socket_send(  client->sock
                                              , &client->buffer[client->chunk_left]
                                              , content_left);
    if(send_size <= 0)
    {
        asc_log_error(MSG("failed to send response to client:%d [%s]")
                      , asc_socket_fd(client->sock), asc_socket_error());
        on_client_close(client);
        return;
    }
    client->chunk_left += send_size;

    if(client->chunk_left == client->response_size)
    {
        client->response_size = 0;
        client->chunk_left = 0;

        if(client->idx_content)
            asc_socket_set_on_ready(client->sock, on_ready_send_content);
        else
        {
            asc_socket_set_on_ready(client->sock, NULL);

            if(client->is_connection_close)
                on_client_close(client);
        }
    }
}

static const char * http_code(int code)
{
    switch(code)
    {
        case 101: return "Switching Protocols";

        case 200: return "OK";

        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";

        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";

        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";

        default:  return "Status Code Undefined";
    }
}

static int method_send(module_data_t *mod)
{
    asc_assert(lua_islightuserdata(lua, 2), MSG(":send() client instance required"));
    http_client_t *client = lua_touserdata(lua, 2);

    const int response = 3;

    lua_getfield(lua, response, __version);
    const char *version = lua_isstring(lua, -1) ? lua_tostring(lua, -1) : "HTTP/1.1";
    lua_pop(lua, 1); // version

    lua_getfield(lua, response, __code);
    const int code = lua_tonumber(lua, -1);
    lua_pop(lua, 1); // code

    lua_getfield(lua, response, __message);
    const char *message = lua_isstring(lua, -1) ? lua_tostring(lua, -1) : http_code(code);
    lua_pop(lua, 1); // message

    ssize_t skip;
    skip = snprintf(client->buffer, HTTP_BUFFER_SIZE, "%s %d %s\r\n", version, code, message);

    skip += snprintf(&client->buffer[skip]
                     , HTTP_BUFFER_SIZE - skip
                     , "Server: %s\r\n"
                     , mod->server_name);

    lua_getfield(lua, response, __headers);
    if(lua_istable(lua, -1))
    {
        lua_foreach(lua, -2)
        {
            const char *header = lua_tostring(lua, -1);
            skip += snprintf(&client->buffer[skip], HTTP_BUFFER_SIZE - skip, "%s\r\n", header);

            if(!strncasecmp(header, __connection, sizeof(__connection) - 1))
            {
                const char *val = &header[sizeof(__connection) - 1];
                if(!strncasecmp(val, __close, sizeof(__close) - 1))
                    client->is_connection_close = true;
            }
        }
    }
    lua_pop(lua, 1); // headers

    lua_getfield(lua, response, __content);
    if(lua_isstring(lua, -1))
    {
        const int content_length = luaL_len(lua, -1);
        skip += snprintf(  &client->buffer[skip]
                         , HTTP_BUFFER_SIZE - skip
                         , "%s%d\r\n"
                         , __content_length, content_length);

        lua_pushvalue(lua, -1);
        client->idx_content = luaL_ref(lua, LUA_REGISTRYINDEX);
    }
    lua_pop(lua, 1); // content

    if(client->is_websocket && code == 101)
    {
        skip += snprintf(  &client->buffer[skip]
                         , HTTP_BUFFER_SIZE - skip
                         , "%s%s\r\n"
                         , __upgrade, __websocket);

        skip += snprintf(  &client->buffer[skip]
                         , HTTP_BUFFER_SIZE - skip
                         , "%s%s\r\n"
                         , __connection, "Upgrade");

        if(client->websocket.accept_key)
        {
            skip += snprintf(  &client->buffer[skip]
                             , HTTP_BUFFER_SIZE - skip
                             , "Sec-WebSocket-Accept: %s\r\n"
                             , client->websocket.accept_key);

            free(client->websocket.accept_key);
            client->websocket.accept_key = NULL;
        }

        asc_socket_set_on_read(client->sock, on_client_read_websocket);
    }

    else if(code == 301 || code == 302)
    {
        lua_getfield(lua, response, "location");
        asc_assert(lua_isstring(lua, -1), ":send() location required");
        const char *location = lua_tostring(lua, -1);
        skip += snprintf(  &client->buffer[skip]
                         , HTTP_BUFFER_SIZE - skip
                         , "Location: %s\r\n", location);
        lua_pop(lua, 1); // location
    }

    skip += snprintf(&client->buffer[skip], HTTP_BUFFER_SIZE - skip, "\r\n");

    client->chunk_left = 0;
    client->response_size = skip;
    asc_socket_set_on_ready(client->sock, on_ready_send_response);

    return 0;
}

/*
 *  oooooooo8 ooooooooooo oooooooooo ooooo  oooo ooooooooooo oooooooooo
 * 888         888    88   888    888 888    88   888    88   888    888
 *  888oooooo  888ooo8     888oooo88   888  88    888ooo8     888oooo88
 *         888 888    oo   888  88o     88888     888    oo   888  88o
 * o88oooo888 o888ooo8888 o888o  88o8    888     o888ooo8888 o888o  88o8
 *
 */

static void on_server_close(void *arg)
{
    module_data_t *mod = arg;

    if(!mod->sock)
        return;

    asc_socket_close(mod->sock);
    mod->sock = NULL;

    if(mod->clients)
    {
        http_client_t *prev_client = NULL;
        for(  asc_list_first(mod->clients)
            ; !asc_list_eol(mod->clients)
            ; asc_list_first(mod->clients))
        {
            http_client_t *client = asc_list_data(mod->clients);
            asc_assert(client != prev_client
                       , MSG("loop on on_server_close() client:%p")
                       , client);
            on_client_close(client);
            prev_client = client;
        }

        asc_list_destroy(mod->clients);
        mod->clients = NULL;
    }

    if(mod->idx_self)
    {
        luaL_unref(lua, LUA_REGISTRYINDEX, mod->idx_self);
        mod->idx_self = 0;
    }
}

static void on_server_accept(void *arg)
{
    module_data_t *mod = arg;

    http_client_t *client = calloc(1, sizeof(http_client_t));
    client->mod = mod;

    if(!asc_socket_accept(mod->sock, &client->sock, client))
    {
        free(client);
        on_server_close(mod);
        astra_abort(); // TODO: try to restart server
    }

    asc_list_insert_tail(mod->clients, client);

    asc_socket_set_on_read(client->sock, on_client_read);
    asc_socket_set_on_close(client->sock, on_client_close);
}

/*
 * oooo     oooo  ooooooo  ooooooooo  ooooo  oooo ooooo       ooooooooooo
 *  8888o   888 o888   888o 888    88o 888    88   888         888    88
 *  88 888o8 88 888     888 888    888 888    88   888         888ooo8
 *  88  888  88 888o   o888 888    888 888    88   888      o  888    oo
 * o88o  8  o88o  88ooo88  o888ooo88    888oo88   o888ooooo88 o888ooo8888
 *
 */

static int method_data(module_data_t *mod)
{
    asc_assert(lua_islightuserdata(lua, 2), MSG(":data() client instance required"));
    http_client_t *client = lua_touserdata(lua, 2);

    if(!client->idx_data)
    {
        lua_newtable(lua);
        client->idx_data = luaL_ref(lua, LUA_REGISTRYINDEX);
    }
    lua_rawgeti(lua, LUA_REGISTRYINDEX, client->idx_data);
    return 1;
}

static int method_close(module_data_t *mod)
{
    if(lua_gettop(lua) == 1)
    {
        on_server_close(mod);
    }
    else
    {
        asc_assert(lua_islightuserdata(lua, 2), MSG(":close() client instance required"));
        http_client_t *client = lua_touserdata(lua, 2);
        on_client_close(client);
    }

    return 0;
}

static void module_init(module_data_t *mod)
{
    module_option_string("addr", &mod->addr, NULL);
    if(!mod->addr || !mod->addr[0])
        mod->addr = "0.0.0.0";

    mod->port = 80;
    module_option_number("port", &mod->port);

    mod->server_name = "Astra";
    module_option_string("server_name", &mod->server_name, NULL);

    // store callback in registry
    lua_getfield(lua, 2, __callback);
    asc_assert(lua_isfunction(lua, -1), MSG("option 'callback' is required"));
    lua_pop(lua, 1); // callback

    // store self in registry
    lua_pushvalue(lua, 3);
    mod->idx_self = luaL_ref(lua, LUA_REGISTRYINDEX);

    mod->clients = asc_list_init();

    mod->sock = asc_socket_open_tcp4(mod);
    asc_socket_set_reuseaddr(mod->sock, 1);
    if(!asc_socket_bind(mod->sock, mod->addr, mod->port))
    {
        on_server_close(mod);
        astra_abort(); // TODO: try to restart server
    }
    asc_socket_listen(mod->sock, on_server_accept, on_server_close);
}

static void module_destroy(module_data_t *mod)
{
    if(mod->idx_self == 0)
        return;

    on_server_close(mod);
}

MODULE_LUA_METHODS()
{
    { "send", method_send },
    { "close", method_close },
    { "data", method_data }
};

MODULE_LUA_REGISTER(http_server)
