/*
 * Astra Module: HTTP Server
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

/*
 * Module Name:
 *      http_server
 *
 * Module Options:
 *      addr        - string, server IP address
 *      port        - number, server port
 *      callback    - function,
 *
 * Module Methods:
 *      port()      - return number, server port
 *      close(client)
 *                  - close client connection
 *      send(client, data)
 *                  - stream response to client. data - table:
 *                    * version - string, protocol version. default: "HTTP/1.1"
 *                    * code - number, response code. default: 200
 *                    * message - string, response code description. default: "OK"
 *                    * headers - table (list of strings), response headers
 *                    * content - string, response body from the string
 *                    * file - string, full path to file, reponse body from the file
 *                    * upstream - object, stream instance returned by module_instance:stream()
 *      data(client)
 *                  - return table, client data
 */

#include <astra.h>
#include <fcntl.h>
#include "parser.h"

#define MSG(_msg) "[http_server %s:%d] " _msg, mod->addr, mod->port

#define HTTP_BUFFER_SIZE 1024
#define HTTP_BUFFER_FILL 128

#define FRAME_HEADER_SIZE 2
#define FRAME_KEY_SIZE 4
#define FRAME_SIZE8_SIZE 0
#define FRAME_SIZE16_SIZE 2
#define FRAME_SIZE64_SIZE 8

typedef struct
{
    MODULE_STREAM_DATA();

    module_data_t *mod;

    asc_socket_t *sock;

    int idx_request;
    int idx_data;

    int ready_state; // 0 - not, 1 request, 2 - headers, 3 - content

    int is_websocket;
    int is_close;
    int is_keep_alive;
    int content_length;

    FILE *src_file;
    int src_content; // lua reference

    int buffer_fill;
    int buffer_skip;
    char *buffer;

    bool is_socket_busy;
} http_client_t;

struct module_data_t
{
    MODULE_LUA_DATA();

    int idx_self;

    const char *addr;
    int port;

    int buffer_size;
    int buffer_fill;
    int buffer_prefill;

    asc_socket_t *sock;
    asc_list_t *clients;
};

/*
 *   oooooooo8 ooooo       ooooo ooooooooooo oooo   oooo ooooooooooo
 * o888     88  888         888   888    88   8888o  88  88  888  88
 * 888          888         888   888ooo8     88 888o88      888
 * 888o     oo  888      o  888   888    oo   88   8888      888
 *  888oooo88  o888ooooo88 o888o o888ooo8888 o88o    88     o888o
 *
 */

static void get_lua_callback(module_data_t *mod)
{
    lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->__lua.oref);
    lua_getfield(lua, -1, "callback");
    lua_remove(lua, -2);
}

static void on_read_error(void *arg)
{
    http_client_t *client = arg;
    module_data_t *mod = client->mod;

    if(client->idx_request)
    {
        luaL_unref(lua, LUA_REGISTRYINDEX, client->idx_request);
        client->idx_request = 0;
    }

    get_lua_callback(mod);
    lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_self);
    lua_pushlightuserdata(lua, client);
    lua_pushnil(lua);
    lua_call(lua, 3, 0);

    if(client->idx_data)
        luaL_unref(lua, LUA_REGISTRYINDEX, client->idx_data);
    lua_gc(lua, LUA_GCCOLLECT, 0);

    if(client->__stream.self)
        __module_stream_destroy(&client->__stream);

    if(client->sock)
        asc_socket_close(client->sock);

    char *buffer = client->buffer;
    memset(client, 0, sizeof(http_client_t));
    client->mod = mod;
    client->buffer = buffer;
    client->buffer_fill = mod->buffer_prefill;
}

static void on_read(void *arg)
{
    http_client_t *client = arg;
    module_data_t *mod = client->mod;

    int r = asc_socket_recv(client->sock, client->buffer, mod->buffer_size);
    if(r <= 0)
    {
        if(r == -1)
            asc_log_error(MSG("failed to read a request [%s]"), asc_socket_error());
        on_read_error(client);
        return;
    }

    int request = 0;
    int skip = 0;

    parse_match_t m[4];

    // parse request
    if(client->ready_state == 0)
    {
        if(client->idx_request)
        {
            luaL_unref(lua, LUA_REGISTRYINDEX, client->idx_request);
            client->idx_request = 0;
        }
        client->is_close = 0;
        client->is_keep_alive = 0;
        client->content_length = 0;

        if(!http_parse_request(client->buffer, m))
        {
            get_lua_callback(mod);
            lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_self);
            lua_pushlightuserdata(lua, client);
            lua_newtable(lua);
            lua_pushstring(lua, "failed to parse http request");
            lua_setfield(lua, -2, "message");
            lua_call(lua, 3, 0);
            return;
        }

        lua_newtable(lua);
        request = lua_gettop(lua);
        lua_pushvalue(lua, -1); // duplicate table
        client->idx_request = luaL_ref(lua, LUA_REGISTRYINDEX);

        lua_pushlstring(lua, &client->buffer[m[1].so], m[1].eo - m[1].so);
        lua_setfield(lua, request, "method");
        lua_pushlstring(lua, &client->buffer[m[2].so], m[2].eo - m[2].so);
        lua_setfield(lua, request, "uri");
        lua_pushlstring(lua, &client->buffer[m[3].so], m[3].eo - m[3].so);
        lua_setfield(lua, request, "version");

        lua_pushstring(lua, asc_socket_addr(client->sock));
        lua_setfield(lua, request, "addr");
        lua_pushnumber(lua, asc_socket_port(client->sock));
        lua_setfield(lua, request, "port");

        skip = m[0].eo;
        client->ready_state = 1;

        if(skip >= r)
        {
            lua_pop(lua, 1); // request
            return;
        }
    }

    // parse headers
    if(client->ready_state == 1)
    {
        if(!request)
        {
            lua_rawgeti(lua, LUA_REGISTRYINDEX, client->idx_request);
            request = lua_gettop(lua);
        }

        int headers_count = 0;
        static const char __headers[] = "headers";
        lua_getfield(lua, request, __headers);
        if(lua_isnil(lua, -1))
        {
            lua_pop(lua, 1);
            lua_newtable(lua);
            lua_pushvalue(lua, -1);
            lua_setfield(lua, request, __headers);
        }
        else
            headers_count = luaL_len(lua, -1);

        const int headers = lua_gettop(lua);

        while(skip < r && http_parse_header(&client->buffer[skip], m))
        {
            const int so = m[1].so;
            const int length = m[1].eo - so;
            if(!length)
            {
                skip += m[0].eo;
                client->ready_state = 2;
                break;
            }
            const char *header = &client->buffer[skip + so];

            static const char __content_length[] = "Content-Length: ";
            static const char __connection[] = "Connection: ";
            static const char __close[] = "close";
            static const char __keep_alive[] = "keep-alive";
            static const char __upgrade[] = "Upgrade: ";
            static const char __websocket[] = "websocket";
            if(!strncasecmp(header, __connection, sizeof(__connection) - 1))
            {
                const char *val = &header[sizeof(__connection) - 1];
                if(!strncasecmp(val, __close, sizeof(__close) - 1))
                    client->is_close = 1;
                else if(!strncasecmp(val, __keep_alive, sizeof(__keep_alive) - 1))
                    client->is_keep_alive = 1;
            }
            else if(!strncasecmp(header, __content_length, sizeof(__content_length) - 1))
            {
                const char *val = &header[sizeof(__content_length) - 1];
                client->content_length = strtoul(val, NULL, 10);
            }
            else if(!strncasecmp(header, __upgrade, sizeof(__upgrade) - 1))
            {
                const char *val = &header[sizeof(__upgrade) - 1];
                if(!strncasecmp(val, __websocket, sizeof(__websocket) - 1))
                    client->is_websocket = 1;
            }

            ++headers_count;
            lua_pushnumber(lua, headers_count);
            lua_pushlstring(lua, header, length);
            lua_settable(lua, headers);
            skip += m[0].eo;
        }
        lua_pop(lua, 1); // headers

        if(client->ready_state == 2)
        {
            luaL_unref(lua, LUA_REGISTRYINDEX, client->idx_request);
            client->idx_request = 0;

            get_lua_callback(mod);
            lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_self);
            lua_pushlightuserdata(lua, client);
            lua_pushvalue(lua, request);
            lua_call(lua, 3, 0);
        }

        lua_pop(lua, 1); // request

        if(skip >= r || client->sock == NULL)
            return;
    }

    // content
    if(client->ready_state == 2)
    {
        // Content-Length
        if(client->content_length)
        {
            if(client->content_length > 0)
            {
                const int r_skip = r - skip;
                get_lua_callback(mod);
                lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_self);
                lua_pushlightuserdata(lua, client);

                if(client->content_length > r_skip)
                {
                    lua_pushlstring(lua, &client->buffer[skip], r_skip);
                    lua_call(lua, 3, 0);
                    client->content_length -= r_skip;
                }
                else
                {
                    lua_pushvalue(lua, -3);
                    lua_pushvalue(lua, -3);
                    lua_pushvalue(lua, -3);
                    lua_pushlstring(lua, &client->buffer[skip], client->content_length);
                    lua_call(lua, 3, 0);
                    client->content_length = -1;

                    // content is done
                    client->ready_state = 0;

                    lua_pushstring(lua, "");
                    lua_call(lua, 3, 0);
                }
            }
        }

        // Stream
        else
        {
            get_lua_callback(mod);
            lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_self);
            lua_pushlightuserdata(lua, client);

            if(client->is_websocket)
            {
                uint8_t *key = NULL;
                uint8_t *data = (uint8_t *)client->buffer;
                uint64_t data_size = data[1] & 0x7F;

                if(data_size < 126)
                {
                    key = data + FRAME_HEADER_SIZE + FRAME_SIZE8_SIZE;
                }
                else if(data_size == 126)
                {
                    data_size = (data[2] << 8) | data[3];

                    asc_assert(data_size < ((uint64_t)mod->buffer_size
                                            - FRAME_HEADER_SIZE
                                            - FRAME_SIZE16_SIZE
                                            - FRAME_KEY_SIZE)
                               , MSG("data_size limit"));

                    key = data + FRAME_HEADER_SIZE + FRAME_SIZE16_SIZE;
                }
                else if(data_size == 127)
                {
                    data_size = (  ((uint64_t)data[2] << 56)
                                 | ((uint64_t)data[3] << 48)
                                 | ((uint64_t)data[4] << 40)
                                 | ((uint64_t)data[5] << 32)
                                 | ((uint64_t)data[6] << 24)
                                 | ((uint64_t)data[7] << 16)
                                 | ((uint64_t)data[8] << 8 )
                                 | ((uint64_t)data[9]      ));

                    asc_assert(data_size < ((uint64_t)mod->buffer_size
                                            - FRAME_HEADER_SIZE
                                            - FRAME_SIZE64_SIZE
                                            - FRAME_KEY_SIZE)
                               , MSG("data_size limit"));

                    key = data + FRAME_HEADER_SIZE + FRAME_SIZE64_SIZE;
                }
                data = key + FRAME_KEY_SIZE;

                // TODO: check FIN
                // TODO: check opcode
                // TODO: luaL_Buffer (to join several buffers)

                for(size_t i = 0; i < data_size; ++i)
                    data[i] ^= key[i % 4];

                lua_pushlstring(lua, (char *)data, data_size);
            }
            else
                lua_pushlstring(lua, &client->buffer[skip], r - skip);

            lua_call(lua, 3, 0);
        }
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

    lua_rawgeti(lua, LUA_REGISTRYINDEX, client->src_content);
    const int content_size = luaL_len(lua, -1);
    const char *content = lua_tostring(lua, -1);

    const int content_left = content_size - client->buffer_skip;
    const int content_send = (content_left > mod->buffer_size)
                           ? mod->buffer_size
                           : content_left;


    const ssize_t send_size = asc_socket_send(client->sock
                                              , (void *)&content[client->buffer_skip]
                                              , content_send);
    if(send_size <= 0)
    {
        asc_log_error(MSG("failed to send content to client:%d [%s]")
                      , asc_socket_fd(client->sock), asc_socket_error());
        on_read_error(client);
        return;
    }
    client->buffer_skip += send_size;

    if(client->buffer_skip == content_size)
    {
        luaL_unref(lua, LUA_REGISTRYINDEX, client->src_content);
        client->src_content = 0;
        client->buffer_skip = 0;
        asc_socket_set_on_ready(client->sock, NULL);
    }
}

static void on_ready_send_file(void *arg)
{
    http_client_t *client = arg;
    module_data_t *mod = client->mod;

    if(client->src_file)
    {
        const int capacity_size = mod->buffer_size - client->buffer_skip;
        if(capacity_size <= 0)
        {
            // TODO: overflow
            return;
        }

        const int block_size = fread(&client->buffer[client->buffer_skip], 1, capacity_size
                                     , client->src_file);
        if(block_size <= 0)
        {
            asc_log_error(MSG("failed to read source file [%s]"), strerror(errno));
            on_read_error(client);
            return;
        }

        client->buffer_skip += block_size;
    }

    const ssize_t send_size = asc_socket_send(client->sock, client->buffer, client->buffer_skip);
    if(send_size <= 0)
    {
        asc_log_warning(MSG("failed to send file part (%d bytes) to client:%d [%s]")
                        , client->buffer_skip, asc_socket_fd(client->sock), asc_socket_error());
        on_read_error(client);
        return;
    }

    if(send_size == client->buffer_skip)
    {
        client->buffer_skip = 0;
        asc_socket_set_on_ready(client->sock, NULL);
    }
    else
    {
        client->buffer_skip -= send_size;
        memmove(client->buffer, &client->buffer[send_size], client->buffer_skip);
    }

    if(client->src_file && feof(client->src_file))
        client->src_file = NULL;
}

static void on_ready_send_ts(void *arg)
{
    http_client_t *client = arg;
    module_data_t *mod = client->mod;

    const ssize_t send_size = asc_socket_send(client->sock
                                              , client->buffer, client->buffer_skip);
    if(send_size > 0)
    {
        client->buffer_skip -= send_size;
        if(client->buffer_skip > 0)
            memmove(client->buffer, &client->buffer[send_size], client->buffer_skip);
    }
    else if(send_size == -1)
    {
        asc_log_warning(MSG("failed to send ts (%d bytes) to client:%d [%s]")
                        , client->buffer_skip, asc_socket_fd(client->sock)
                        , asc_socket_error());
        on_read_error(client);
        return;
    }

    if(client->buffer_skip == 0)
    {
        if(client->is_socket_busy)
        {
            asc_socket_set_on_ready(client->sock, NULL);
            client->is_socket_busy = false;
        }
    }
    else
    {
        if(!client->is_socket_busy)
        {
            asc_socket_set_on_ready(client->sock, on_ready_send_ts);
            client->is_socket_busy = true;
        }
    }
}

static void on_ts(void *arg, const uint8_t *ts)
{
    http_client_t *client = arg;
    module_data_t *mod = client->mod;

    if(client->buffer_skip > mod->buffer_size - TS_PACKET_SIZE)
    {
        client->buffer_skip = 0;
        if(client->is_socket_busy)
        {
            asc_socket_set_on_ready(client->sock, NULL);
            client->is_socket_busy = false;
        }
        return;
    }

    memcpy(&client->buffer[client->buffer_skip], ts, TS_PACKET_SIZE);
    client->buffer_skip += TS_PACKET_SIZE;

    if(!client->is_socket_busy && client->buffer_skip >= client->buffer_fill)
    {
        if(mod->buffer_fill != client->buffer_fill)
            client->buffer_fill = mod->buffer_fill;
        on_ready_send_ts(arg);
    }
}

static void buffer_set_text(char **buffer, int capacity
                            , const char *default_value, int default_len
                            , int is_end)
{
    // TODO: check capacity
    __uarg(capacity);

    char *ptr = *buffer;
    if(!lua_isnil(lua, -1))
    {
        default_value = lua_tostring(lua, -1);
        default_len = luaL_len(lua, -1);
    }
    if(default_len > 0)
    {
        strcpy(ptr, default_value);
        ptr += default_len;
    }
    if(is_end)
    {
        ptr[0] = '\r';
        ptr[1] = '\n';
        ptr += 2;
    }
    else
    {
        ptr[0] = ' ';
        ptr += 1;
    }
    *buffer = ptr;
}

static int method_send(module_data_t *mod)
{
    if(lua_type(lua, 2) != LUA_TLIGHTUSERDATA)
    {
        asc_log_error(MSG(":send() client instance is required"));
        astra_abort();
    }
    http_client_t *client = lua_touserdata(lua, 2);
    if(!client->sock)
        return 0;

    if(lua_type(lua, 3) == LUA_TSTRING)
    {
        int send_ret;
        const char *str = lua_tostring(lua, 3);
        const uint64_t str_size = luaL_len(lua, 3);

        if(client->is_websocket)
        {
            uint8_t *data = (uint8_t *)client->buffer;
            uint8_t frame_header = FRAME_HEADER_SIZE;

            data[0] = 0x81;
            if(str_size <= 125)
            {
                data[1] = str_size & 0xFF;
            }
            else if(str_size <= 0xFFFF)
            {
                data[1] = 126;
                data[2] = (str_size >> 8) & 0xFF;
                data[3] = (str_size     ) & 0xFF;

                frame_header += FRAME_SIZE16_SIZE;
            }
            else
            {
                data[1] = 127;
                data[2] = (str_size >> 56) & 0xFF;
                data[3] = (str_size >> 48) & 0xFF;
                data[4] = (str_size >> 40) & 0xFF;
                data[5] = (str_size >> 32) & 0xFF;
                data[6] = (str_size >> 24) & 0xFF;
                data[7] = (str_size >> 16) & 0xFF;
                data[8] = (str_size >> 8 ) & 0xFF;
                data[9] = (str_size      ) & 0xFF;

                frame_header += FRAME_SIZE64_SIZE;
            }

            memcpy(&data[frame_header], str, str_size);

            send_ret = asc_socket_send(client->sock, (void *)data, frame_header + str_size);
        }
        else
            send_ret = asc_socket_send(client->sock, (void *)str, str_size);

        if(send_ret <= 0)
        {
            asc_log_error(MSG("failed to send data to client:%d [%s]")
                          , asc_socket_fd(client->sock), asc_socket_error());
        }
        return 0;
    }
    else if(lua_type(lua, 3) != LUA_TTABLE)
    {
        asc_log_error(MSG(":send() table is required"));
        astra_abort();
    }

    char *buffer = client->buffer;
    char * const buffer_tail = buffer + mod->buffer_size;

    // version
    lua_getfield(lua, 3, "version");
    static const char d_version[] = "HTTP/1.1";
    buffer_set_text(&buffer, buffer_tail - buffer, d_version, sizeof(d_version) - 1, 0);
    lua_pop(lua, 1);

    // code
    lua_getfield(lua, 3, "code");
    static const char d_code[] = "200";
    buffer_set_text(&buffer, buffer_tail - buffer, d_code, sizeof(d_code) - 1, 0);
    lua_pop(lua, 1);

    // message
    lua_getfield(lua, 3, "message");
    static const char d_message[] = "OK";
    buffer_set_text(&buffer, buffer_tail - buffer, d_message, sizeof(d_message) - 1, 1);
    lua_pop(lua, 1);

    // headers
    lua_getfield(lua, 3, "headers");
    if(lua_type(lua, -1) == LUA_TTABLE)
    {
        for(lua_pushnil(lua); lua_next(lua, -2); lua_pop(lua, 1))
            buffer_set_text(&buffer, buffer_tail - buffer, "", 0, 1);
    }
    lua_pop(lua, 1);

    // empty line
    lua_pushnil(lua);
    buffer_set_text(&buffer, buffer_tail - buffer, "", 0, 1);
    lua_pop(lua, 1);

    const int header_size = buffer - client->buffer;
    if(asc_socket_send(client->sock, client->buffer, header_size) <= 0)
    {
        asc_log_error(MSG("failed to send response to client:%d [%s]")
                      , asc_socket_fd(client->sock), asc_socket_error());
        return 0;
    }

    // content
    lua_getfield(lua, 3, "content");
    if(!lua_isnil(lua, -1))
    {
        lua_pushvalue(lua, -1);
        client->src_content = luaL_ref(lua, LUA_REGISTRYINDEX);
        asc_socket_set_on_ready(client->sock, on_ready_send_content);
    }
    lua_pop(lua, 1);

    // file
    lua_getfield(lua, 3, "file");
    if(!lua_isnil(lua, -1))
    {
        luaL_Stream *stream = luaL_checkudata(lua, -1, LUA_FILEHANDLE);
        client->src_file = stream->f;
        asc_socket_set_on_ready(client->sock, on_ready_send_file);
    }
    lua_pop(lua, 1);

    // upsrteam
    lua_getfield(lua, 3, "upstream");
    if(!lua_isnil(lua, -1))
    {
        // like module_stream_init()
        client->__stream.self = (void *)client;
        client->__stream.on_ts = (void (*)(module_data_t *, const uint8_t *))on_ts;
        __module_stream_init(&client->__stream);
        __module_stream_attach(lua_touserdata(lua, -1), &client->__stream);
    }
    lua_pop(lua, 1);

    if(!client->is_websocket)
        client->ready_state = 0;

    return 0;
}

static int method_data(module_data_t *mod)
{
    if(lua_type(lua, 2) != LUA_TLIGHTUSERDATA)
    {
        asc_log_error(MSG(":data() client instance is required"));
        astra_abort();
    }
    http_client_t *client = lua_touserdata(lua, 2);

    if(!client->idx_data)
    {
        lua_newtable(lua);
        lua_pushvalue(lua, -1);
        client->idx_data = luaL_ref(lua, LUA_REGISTRYINDEX);
    }
    else
        lua_rawgeti(lua, LUA_REGISTRYINDEX, client->idx_data);

    return 1;
}

/*
 *  oooooooo8 ooooooooooo oooooooooo ooooo  oooo ooooooooooo oooooooooo
 * 888         888    88   888    888 888    88   888    88   888    888
 *  888oooooo  888ooo8     888oooo88   888  88    888ooo8     888oooo88
 *         888 888    oo   888  88o     88888     888    oo   888  88o
 * o88oooo888 o888ooo8888 o888o  88o8    888     o888ooo8888 o888o  88o8
 *
 */

static void server_close(module_data_t *mod)
{
    for(asc_list_first(mod->clients)
        ; !asc_list_eol(mod->clients)
        ; asc_list_first(mod->clients))
    {
        http_client_t *client = asc_list_data(mod->clients);
        if(client->sock)
            asc_socket_close(client->sock);
        free(client->buffer);
        free(client);
        asc_list_remove_current(mod->clients);
    }

    asc_socket_close(mod->sock);
    mod->sock = NULL;

    asc_list_destroy(mod->clients);
    mod->clients = NULL;

    if(mod->idx_self > 0)
    {
        luaL_unref(lua, LUA_REGISTRYINDEX, mod->idx_self);
        mod->idx_self = 0;
    }
}

static int method_close(module_data_t *mod)
{
    // close self
    if(lua_gettop(lua) < 2)
    {
        server_close(mod);
        return 0;
    }

    // close client
    if(lua_type(lua, 2) != LUA_TLIGHTUSERDATA)
    {
        asc_log_error(MSG(":close() client instance required"));
        astra_abort();
    }

    http_client_t *client = lua_touserdata(lua, 2);
    if(client->sock)
        on_read_error(client);

    return 0;
}

static void on_accept_error(void *arg)
{
    server_close(arg);
}

static void on_accept(void *arg)
{
    module_data_t *mod = arg;

    http_client_t *client = NULL;
    asc_list_for(mod->clients)
    {
        client = asc_list_data(mod->clients);
        if(!client->sock)
            break;
        client = NULL;
    }

    if(!client)
    {
        client = calloc(1, sizeof(http_client_t));
        client->mod = mod;
        client->buffer = malloc(mod->buffer_size);
        client->buffer_fill = mod->buffer_prefill;
        asc_list_insert_tail(mod->clients, client);
    }

    if(!asc_socket_accept(mod->sock, &client->sock, client))
    {
        on_accept_error(mod);
        return;
    }

    asc_socket_set_on_read(client->sock, on_read);
    asc_socket_set_on_close(client->sock, on_read_error);

    if(asc_log_is_debug())
    {
        asc_log_debug(MSG("client connected from %s:%d")
                      , asc_socket_addr(client->sock), asc_socket_port(client->sock));
    }
}

static int method_port(module_data_t *mod)
{
    const int port = asc_socket_port(mod->sock);
    lua_pushnumber(lua, port);
    return 1;
}

/*
 * oooo     oooo  ooooooo  ooooooooo  ooooo  oooo ooooo       ooooooooooo
 *  8888o   888 o888   888o 888    88o 888    88   888         888    88
 *  88 888o8 88 888     888 888    888 888    88   888         888ooo8
 *  88  888  88 888o   o888 888    888 888    88   888      o  888    oo
 * o88o  8  o88o  88ooo88  o888ooo88    888oo88   o888ooooo88 o888ooo8888
 *
 */

static void module_init(module_data_t *mod)
{
    int value_length = 0;
    if(!module_option_string("addr", &mod->addr, &value_length) || !value_length)
        mod->addr = "0.0.0.0";
    if(!module_option_number("port", &mod->port))
        mod->port = 80;

    if(!module_option_number("buffer_size", &mod->buffer_size))
        mod->buffer_size = HTTP_BUFFER_SIZE;
    mod->buffer_size *= 1024;

    if(!module_option_number("buffer_fill", &mod->buffer_fill))
        mod->buffer_fill = HTTP_BUFFER_FILL;
    mod->buffer_fill *= 1024;

    if(mod->buffer_fill >= mod->buffer_size)
    {
        asc_log_error(MSG("option 'buffer_fill' must be less then 'buffer_size'"));
        astra_abort();
    }

    if(!module_option_number("buffer_prefill", &mod->buffer_prefill))
        mod->buffer_prefill = mod->buffer_fill;
    else
    {
        mod->buffer_prefill *= 1024;
        if(mod->buffer_prefill >= mod->buffer_size)
        {
            asc_log_error(MSG("option 'buffer_prefill' must be less then 'buffer_size'"));
            astra_abort();
        }
    }

    // store callback in registry
    lua_getfield(lua, 2, "callback");
    if(lua_type(lua, -1) != LUA_TFUNCTION)
    {
        asc_log_error(MSG("option 'callback' is required"));
        astra_abort();
    }
    lua_pop(lua, 1); // callback

    // store self in registry
    lua_pushvalue(lua, 3);
    mod->idx_self = luaL_ref(lua, LUA_REGISTRYINDEX);

    mod->clients = asc_list_init();

    mod->sock = asc_socket_open_tcp4(mod);
    asc_socket_set_reuseaddr(mod->sock, 1);
    if(!asc_socket_bind(mod->sock, mod->addr, mod->port))
    {
        server_close(mod);
        astra_abort();
    }
    asc_socket_listen(mod->sock, on_accept, on_accept_error);
}

static void module_destroy(module_data_t *mod)
{
    if(mod->idx_self)
        server_close(mod);
}

MODULE_LUA_METHODS()
{
    { "port", method_port },
    { "close", method_close },
    { "send", method_send },
    { "data", method_data }
};

MODULE_LUA_REGISTER(http_server)
