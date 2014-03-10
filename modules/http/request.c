/*
 * Astra Module: HTTP Request
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
 *      http_request
 *
 * Module Options:
 *      host        - string, server hostname or IP address
 *      port        - number, server port (default: 80)
 *      callback    - function,
 *      stream      - boolean, true to read MPEG-TS stream
 *      method      - string, method (default: "GET")
 *      path        - string, request path
 *      version     - string, HTTP version (default: "HTTP/1.1")
 */

#include <astra.h>
#include "parser.h"
#include <sys/socket.h>

#define MSG(_msg) "[http_request] " _msg
#define HTTP_BUFFER_SIZE 8192

struct module_data_t
{
    MODULE_LUA_DATA();
    MODULE_STREAM_DATA();

    int timeout_ms;
    bool is_stream;

    int idx_self;

    const char *method;
    bool is_connection_close;
    bool is_connection_keep_alive;

    asc_socket_t *sock;
    asc_timer_t *timeout;
    bool is_connected;

    char buffer[HTTP_BUFFER_SIZE];

    int status_code;
    int response_idx;
    bool is_status_line;
    bool is_response_headers;

    bool is_chunked;
    bool is_content_length;

    size_t chunk_left; // is_chunked || is_content_length

    asc_thread_t *thread;
    string_buffer_t *content;
};

static const char __method[] = "method";
static const char __path[] = "path";
static const char __version[] = "version";
static const char __headers[] = "headers";
static const char __content[] = "content";
static const char __callback[] = "callback";
static const char __stream[] = "stream";
static const char __code[] = "code";
static const char __message[] = "message";

static const char __default_method[] = "GET";

static const char __content_length[] = "Content-Length: ";

static const char __transfer_encoding[] = "Transfer-Encoding: ";
static const char __chunked[] = "chunked";

static const char __connection[] = "Connection: ";
static const char __close[] = "close";
static const char __keep_alive[] = "keep-alive";

static void on_close(void *);

static void get_lua_callback(module_data_t *mod)
{
    lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->__lua.oref);
    lua_getfield(lua, -1, "callback");
    lua_remove(lua, -2);
}

static void call_error(module_data_t *mod, const char *msg)
{
    get_lua_callback(mod);

    lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_self);
    lua_newtable(lua);
    lua_pushnumber(lua, 0);
    lua_setfield(lua, -2, __code);
    lua_pushstring(lua, msg);
    lua_setfield(lua, -2, __message);

    lua_call(lua, 2, 0);
}

void timeout_callback(void *arg)
{
    module_data_t *mod = arg;

    asc_timer_destroy(mod->timeout);
    mod->timeout = NULL;

    if(!mod->is_connected)
        call_error(mod, "connection timeout");
    else
        call_error(mod, "response timeout");

    on_close(mod);
}

static void on_close(void *arg)
{
    module_data_t *mod = arg;

    if(mod->timeout)
    {
        asc_timer_destroy(mod->timeout);
        mod->timeout = NULL;
    }

    if(mod->sock)
    {
        asc_socket_close(mod->sock);
        mod->sock = NULL;
    }

    if(mod->response_idx)
    {
        luaL_unref(lua, LUA_REGISTRYINDEX, mod->response_idx);
        mod->response_idx = 0;
    }

    if(mod->idx_self)
    {
        // get_lua_callback(mod);
        // lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_self);
        // lua_pushnil(lua);

        luaL_unref(lua, LUA_REGISTRYINDEX, mod->idx_self);
        mod->idx_self = 0;

        // lua_call(lua, 2, 0);
    }

    if(mod->content)
    {
        string_buffer_free(mod->content);
        mod->content = NULL;
    }
}

/*
 *  oooooooo8 ooooooooooo oooooooooo  ooooooooooo      o      oooo     oooo
 * 888        88  888  88  888    888  888    88      888      8888o   888
 *  888oooooo     888      888oooo88   888ooo8       8  88     88 888o8 88
 *         888    888      888  88o    888    oo    8oooo88    88  888  88
 * o88oooo888    o888o    o888o  88o8 o888ooo8888 o88o  o888o o88o  8  o88o
 *
 */

static void read_stream_thread(void *arg)
{
    module_data_t *mod = arg;

    asc_thread_while(mod->thread)
    {
        //
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

static void on_read(void *arg)
{
    module_data_t *mod = arg;

    if(mod->timeout)
    {
        asc_timer_destroy(mod->timeout);
        mod->timeout = NULL;
    }

    ssize_t skip = 0;

    ssize_t size = asc_socket_recv(mod->sock, mod->buffer, sizeof(mod->buffer));
    if(size <= 0)
    {
        on_close(mod);
        return;
    }

    parse_match_t m[4];

    // status line
    if(!mod->is_status_line)
    {
        if(!http_parse_response(mod->buffer, m))
        {
            call_error(mod, "invalid response");
            on_close(mod);
            return;
        }

        lua_newtable(lua);
        mod->response_idx = luaL_ref(lua, LUA_REGISTRYINDEX);
        lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->response_idx);
        const int response = lua_gettop(lua);

        mod->status_code = atoi(&mod->buffer[m[2].so]);
        lua_pushnumber(lua, mod->status_code);
        lua_setfield(lua, response, __code);
        lua_pushlstring(lua, &mod->buffer[m[3].so], m[3].eo - m[3].so);
        lua_setfield(lua, response, __message);

        skip += m[0].eo;
        mod->is_status_line = true;

        lua_pop(lua, 1); // response

        if(skip >= size)
            return;
    }

    // response headers
    if(!mod->is_response_headers)
    {
        lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->response_idx);
        const int response = lua_gettop(lua);

        lua_getfield(lua, response, __headers);
        if(lua_isnil(lua, -1))
        {
            lua_pop(lua, 1);
            lua_newtable(lua);
            lua_pushvalue(lua, -1);
            lua_setfield(lua, response, __headers);
        }
        int headers_count = luaL_len(lua, -1);
        const int headers = lua_gettop(lua);

        while(skip < size && http_parse_header(&mod->buffer[skip], m))
        {
            const size_t so = m[1].so;
            const size_t length = m[1].eo - so;

            if(!length)
            { /* empty line */
                skip += m[0].eo;
                mod->is_response_headers = true;
                break;
            }

            const char *header = &mod->buffer[skip + so];
            if(!strncasecmp(header, __transfer_encoding, sizeof(__transfer_encoding) - 1))
            {
                const char *val = &header[sizeof(__transfer_encoding) - 1];
                if(!strncasecmp(val, __chunked, sizeof(__chunked) - 1))
                    mod->is_chunked = 1;
            }
            else if(!strncasecmp(header, __content_length, sizeof(__content_length) - 1))
            {
                const char *val = &header[sizeof(__content_length) - 1];
                mod->is_content_length = true;
                mod->chunk_left = strtoul(val, NULL, 10);
            }

            ++headers_count;
            lua_pushnumber(lua, headers_count);
            lua_pushlstring(lua, header, length);
            lua_settable(lua, headers);
            skip += m[0].eo;
        }

        lua_pop(lua, 2); // headers + response

        if(mod->is_response_headers)
        {
            if(   (strcasecmp(mod->method, "HEAD") == 0)
               || (mod->status_code >= 100 && mod->status_code < 200)
               || (mod->status_code == 204)
               || (mod->status_code == 304))
            {
                // callback

                if(mod->is_connection_close)
                    on_close(mod);

                return;
            }
        }

        if(skip >= size)
            return;
    }

    // content
    if(mod->is_stream && mod->status_code == 200)
    {
        asc_socket_set_nonblock(mod->sock, false);

        get_lua_callback(mod);
        lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_self);
        lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->response_idx);
        lua_pushboolean(lua, mod->is_stream);
        lua_setfield(lua, -2, __stream);
        lua_call(lua, 2, 0);

        asc_thread_init(&mod->thread, read_stream_thread, mod);
        return;
    }

    if(!mod->content)
        mod->content = string_buffer_alloc();

    // Transfer-Encoding: chunked
    if(mod->is_chunked)
    {
        while(skip < size)
        {
            if(!mod->chunk_left)
            {
                if(!http_parse_chunk(&mod->buffer[skip], m))
                {
                    call_error(mod, "invalid chunk");
                    on_close(mod);
                    return;
                }

                mod->chunk_left = 0;
                for(size_t i = m[1].so; i < m[1].eo; ++i)
                {
                    char c = mod->buffer[skip + i];
                    if(c >= '0' && c <= '9')
                        mod->chunk_left = (mod->chunk_left << 4) | (c - '0');
                    else if(c >= 'a' && c <= 'f')
                        mod->chunk_left = (mod->chunk_left << 4) | (c - 'a' + 0x0A);
                    else if(c >= 'A' && c <= 'F')
                        mod->chunk_left = (mod->chunk_left << 4) | (c - 'A' + 0x0A);
                }
                skip += m[0].eo;

                if(!mod->chunk_left)
                {
                    get_lua_callback(mod);
                    lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_self);
                    lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->response_idx);
                    string_buffer_push(lua, mod->content);
                    mod->content = NULL;
                    lua_setfield(lua, -2, __content);
                    lua_call(lua, 2, 0);

                    if(mod->is_connection_close)
                        on_close(mod);
                    return;
                }

                mod->chunk_left += 2;
            }

            const size_t tail = size - skip;
            if(mod->chunk_left <= tail)
            {
                string_buffer_addlstring(mod->content, &mod->buffer[skip], mod->chunk_left - 2);

                skip += mod->chunk_left;
                mod->chunk_left = 0;
            }
            else
            {
                string_buffer_addlstring(mod->content, &mod->buffer[skip], tail);
                mod->chunk_left -= tail;
                return;
            }
        }

        return;
    }

    // Content-Length: *
    if(mod->is_content_length)
    {
        const size_t tail = size - skip;

        if(mod->chunk_left > tail)
        {
            string_buffer_addlstring(mod->content
                                     , &mod->buffer[skip]
                                     , tail);
            mod->chunk_left -= tail;
        }
        else
        {
            string_buffer_addlstring(mod->content
                                     , &mod->buffer[skip]
                                     , mod->chunk_left);
            mod->chunk_left = 0;

            get_lua_callback(mod);
            lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_self);
            lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->response_idx);
            string_buffer_push(lua, mod->content);
            mod->content = NULL;
            lua_setfield(lua, -2, __content);
            lua_call(lua, 2, 0);

            if(mod->is_connection_close)
                on_close(mod);
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

static void on_connect(void *arg)
{
    module_data_t *mod = arg;
    mod->is_connected = true;

    asc_timer_destroy(mod->timeout);
    mod->timeout = asc_timer_init(mod->timeout_ms, timeout_callback, mod);

    lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->__lua.oref);

    ssize_t buffer_skip;

    lua_getfield(lua, -1, __method);
    mod->method = lua_isstring(lua, -1) ? lua_tostring(lua, -1) : __default_method;
    lua_pop(lua, 1);

    lua_getfield(lua, -1, __path);
    const char *path = lua_isstring(lua, -1) ? lua_tostring(lua, -1) : "/";
    lua_pop(lua, 1);

    // TODO: deprecated
    lua_getfield(lua, -1, "uri");
    if(lua_isstring(lua, -1))
    {
        asc_log_warning(MSG("option 'uri' is deprecated, use 'path' instead"));
        path = lua_tostring(lua, -1);
    }
    lua_pop(lua, 1);

    lua_getfield(lua, -1, __version);
    const char *version = lua_isstring(lua, -1) ? lua_tostring(lua, -1) : "HTTP/1.1";
    lua_pop(lua, 1);

    buffer_skip = snprintf(mod->buffer, sizeof(mod->buffer)
                           , "%s %s %s\r\n"
                           , mod->method, path, version);

    lua_getfield(lua, -1, __headers);
    if(lua_istable(lua, -1))
    {
        for(lua_pushnil(lua); lua_next(lua, -2); lua_pop(lua, 1))
        {
            const char *h = lua_tostring(lua, -1);
            const size_t hs = luaL_len(lua, -1);

            if(0 == strncasecmp(h, __connection, sizeof(__connection) - 1))
            {
                const char *hp = &h[sizeof(__connection) - 1];
                if(0 == strncasecmp(hp, __close, sizeof(__close) - 1))
                    mod->is_connection_close = true;
                else if(0 == strncasecmp(hp, __keep_alive, sizeof(__keep_alive) - 1))
                    mod->is_connection_keep_alive = true;
            }

            if(hs + 2 >= sizeof(mod->buffer) - buffer_skip)
            {
                if(asc_socket_send(mod->sock, mod->buffer, buffer_skip) != buffer_skip)
                    asc_log_error(MSG("send failed"));
                buffer_skip = 0;
            }

            memcpy(&mod->buffer[buffer_skip], h, hs);
            buffer_skip += hs;
            mod->buffer[buffer_skip + 0] = '\r';
            mod->buffer[buffer_skip + 1] = '\n';
            buffer_skip += 2;
        }
    }
    lua_pop(lua, 1);

    if(2 >= sizeof(mod->buffer) - buffer_skip)
    {
        if(asc_socket_send(mod->sock, mod->buffer, buffer_skip) != buffer_skip)
            asc_log_error(MSG("send failed"));
        buffer_skip = 0;
    }

    mod->buffer[buffer_skip + 0] = '\r';
    mod->buffer[buffer_skip + 1] = '\n';
    buffer_skip += 2;

    if(asc_socket_send(mod->sock, mod->buffer, buffer_skip) != buffer_skip)
        asc_log_error(MSG("send failed"));

    lua_getfield(lua, -1, __content);
    if(lua_isstring(lua, -1))
    {
        const int content_size = luaL_len(lua, -1);
        const char *content = lua_tostring(lua, -1);
        // TODO: send partially
        if(asc_socket_send(mod->sock, content, content_size) != content_size)
            asc_log_error(MSG("send failed"));
    }
    lua_pop(lua, 1);

    lua_pop(lua, 1); /* options */

    asc_socket_set_on_read(mod->sock, on_read);
}

/*
 * oooo     oooo  ooooooo  ooooooooo  ooooo  oooo ooooo       ooooooooooo
 *  8888o   888 o888   888o 888    88o 888    88   888         888    88
 *  88 888o8 88 888     888 888    888 888    88   888         888ooo8
 *  88  888  88 888o   o888 888    888 888    88   888      o  888    oo
 * o88o  8  o88o  88ooo88  o888ooo88    888oo88   o888ooooo88 o888ooo8888
 *
 */

static int method_close(module_data_t *mod)
{
    on_close(mod);
    return 0;
}

static void module_init(module_data_t *mod)
{
    const char *host = NULL;
    module_option_string("host", &host, NULL);
    asc_assert(host != NULL, MSG("option 'host' is required"));

    int port = 80;
    module_option_number("port", &port);

    lua_getfield(lua, 2, __callback);
    asc_assert(lua_isfunction(lua, -1), MSG("option 'callback' is required"));
    lua_pop(lua, 1); // callback

    // store self in registry
    lua_pushvalue(lua, 3);
    mod->idx_self = luaL_ref(lua, LUA_REGISTRYINDEX);

    module_option_boolean(__stream, &mod->is_stream);
    if(mod->is_stream)
        module_stream_init(mod, NULL);

    mod->timeout_ms = 10;
    module_option_number("timeout", &mod->timeout_ms);
    mod->timeout_ms *= 1000;
    mod->timeout = asc_timer_init(mod->timeout_ms, timeout_callback, mod);

    mod->sock = asc_socket_open_tcp4(mod);
    asc_socket_connect(mod->sock, host, port, on_connect, on_close);
}

static void module_destroy(module_data_t *mod)
{
    if(mod->is_stream)
        module_stream_destroy(mod);

    if(mod->idx_self)
        on_close(mod);
}

MODULE_STREAM_METHODS()
MODULE_LUA_METHODS()
{
    MODULE_STREAM_METHODS_REF(),
    { "close", method_close }
};

MODULE_LUA_REGISTER(http_request)
