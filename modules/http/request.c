/*
 * Astra HTTP Server Module
 * http://cesbo.com/
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

/*
 * Module Name:
 *      http_request
 *
 * Module Options:
 *      addr        - string, server IP address
 *      port        - number, server port
 *      callback    - function,
 *      method      - string, HTTP method ("GET" by default)
 *      uri         - string, requested URI
 *      version     - string, HTTP version ("HTTP/1.1" by default)
 */

#include <astra.h>
#include "parser.h"

#define MSG(_msg) "[http_request] " _msg
#define HTTP_BUFFER_SIZE 8192
#define CONNECT_TIMEOUT_INTERVAL (5*1000)
#define REQUEST_TIMEOUT_INTERVAL (60*1000)

struct module_data_t
{
    const char *addr;
    int port;

    asc_socket_t *sock;

    asc_timer_t *timeout_timer;
    int is_connected; // for timeout message

    int idx_self;
    int idx_options;

    int ready_state; // 0 - not, 1 response, 2 - headers, 3 - content

    size_t chunk_left; // is_chunked || is_content_length

    // headers
    int is_close;
    int is_keep_alive;
    int is_chunked;
    int is_content_length;

    char buffer[HTTP_BUFFER_SIZE];
};

static const char __method[] = "method";
static const char __uri[] = "uri";
static const char __version[] = "version";
static const char __headers[] = "headers";
static const char __content[] = "content";
static const char __callback[] = "callback";
static const char __code[] = "code";
static const char __message[] = "message";
static const char __response[] = "__response";

static const char __content_length[] = "Content-Length: ";
static const char __transfer_encoding[] = "Transfer-Encoding: ";
static const char __connection[] = "Connection: ";

static const char __chunked[] = "chunked";
static const char __close[] = "close";
static const char __keep_alive[] = "keep-alive";

/* module code */

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

/* callbacks */

static int method_close(module_data_t *);

void timeout_callback(void *arg)
{
    module_data_t *mod = arg;

    asc_timer_destroy(mod->timeout_timer);
    mod->timeout_timer = NULL;

    lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_self);
    const int self = lua_gettop(lua);
    lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_options);

    // callback
    lua_getfield(lua, -1, __callback);
    lua_pushvalue(lua, self);
    lua_newtable(lua);
    lua_pushnumber(lua, 0);
    lua_setfield(lua, -2, __code);
    switch(mod->is_connected)
    {
        case 0:
            lua_pushstring(lua, "connection timeout");
            break;
        case 1:
            lua_pushstring(lua, "connection failed");
            break;
        case 2:
            lua_pushstring(lua, "response timeout");
            break;
        default:
            lua_pushstring(lua, "unknown error");
            break;
    }
    lua_setfield(lua, -2, __message);
    lua_call(lua, 2, 0);

    lua_pop(lua, 2); // options + self

    method_close(mod);
}

static void call_error(int callback, int self, const char *msg)
{
    lua_pushvalue(lua, callback);
    lua_pushvalue(lua, self);

    lua_newtable(lua);
    lua_pushnumber(lua, 0);
    lua_setfield(lua, -2, __code);
    lua_pushstring(lua, msg);
    lua_setfield(lua, -2, __message);

    lua_call(lua, 2, 0);
}

static void call_nil(int callback, int self)
{
    lua_pushvalue(lua, callback);
    lua_pushvalue(lua, self);

    lua_pushnil(lua);

    lua_call(lua, 2, 0);
}

static void on_read(void *arg, int is_data)
{
    module_data_t *mod = arg;

    asc_timer_destroy(mod->timeout_timer);
    mod->timeout_timer = NULL;

    lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_self);
    const int self = lua_gettop(lua);
    lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_options);
    const int options = lua_gettop(lua);
    lua_getfield(lua, -1, __callback);
    const int callback = lua_gettop(lua);

    if(!is_data)
    {
        call_nil(callback, self);
        lua_pop(lua, 3); // self + options + callback
        method_close(mod);
        return;
    }

    char *buffer = mod->buffer;
    int r = asc_socket_recv(mod->sock, buffer, HTTP_BUFFER_SIZE);
    if(r <= 0)
    {
        call_nil(callback, self);
        lua_pop(lua, 3); // self + options + callback
        method_close(mod);
        return;
    }

    int response = 0;
    int skip = 0;

    parse_match_t m[4];

    // parse response
    if(mod->ready_state == 0)
    {
        if(!http_parse_response(buffer, m))
        {
            call_error(callback, self, "invalid response");
            lua_pop(lua, 3); // self + options + callback
            return;
        }

        lua_newtable(lua);
        response = lua_gettop(lua);
        lua_pushvalue(lua, -1); // duplicate table
        lua_setfield(lua, options, __response);

        lua_pushnumber(lua, atoi(&buffer[m[2].so]));
        lua_setfield(lua, response, __code);
        lua_pushlstring(lua, &buffer[m[3].so], m[3].eo - m[3].so);
        lua_setfield(lua, response, __message);

        skip = m[0].eo;
        mod->ready_state = 1;

        if(skip >= r)
        {
            lua_pop(lua, 4); // self + options + callback + response
            return;
        }
    }

    // parse headers
    if(mod->ready_state == 1)
    {
        if(!response)
        {
            lua_getfield(lua, options, __response);
            response = lua_gettop(lua);
        }

        int headers_count = 0;
        lua_getfield(lua, response, __headers);
        if(lua_isnil(lua, -1))
        {
            lua_pop(lua, 1);
            lua_newtable(lua);
            lua_pushvalue(lua, -1);
            lua_setfield(lua, response, __headers);
        }
        else
        {
            headers_count = luaL_len(lua, -1);
        }
        const int headers = lua_gettop(lua);

        while(skip < r && http_parse_header(&buffer[skip], m))
        {
            const size_t so = m[1].so;
            const size_t length = m[1].eo - so;
            if(!length)
            {
                skip += m[0].eo;
                mod->ready_state = 2;
                break;
            }
            const char *header = &buffer[skip + so];

            if(!strncasecmp(header, __transfer_encoding
                            , sizeof(__transfer_encoding) - 1))
            {
                const char *val = &header[sizeof(__transfer_encoding) - 1];
                if(!strncasecmp(val, __chunked, sizeof(__chunked) - 1))
                    mod->is_chunked = 1;
            }
            else if(!strncasecmp(header, __connection
                                 , sizeof(__connection) - 1))
            {
                const char *val = &header[sizeof(__connection) - 1];
                if(!strncasecmp(val, __close, sizeof(__close) - 1))
                    mod->is_close = 1;
                else if(!strncasecmp(val, __keep_alive
                                     , sizeof(__keep_alive) - 1))
                {
                    mod->is_keep_alive = 1;
                }
            }
            else if(!strncasecmp(header, __content_length
                                 , sizeof(__content_length) - 1))
            {
                const char *val = &header[sizeof(__content_length) - 1];
                mod->is_content_length = 1;
                mod->chunk_left = strtoul(val, NULL, 10);
            }

            ++headers_count;
            lua_pushnumber(lua, headers_count);
            lua_pushlstring(lua, header, length);
            lua_settable(lua, headers);
            skip += m[0].eo;
        }

        lua_pop(lua, 1); // headers

        if(mod->ready_state == 2)
        {
            lua_pushvalue(lua, callback);
            lua_pushvalue(lua, self);
            lua_pushvalue(lua, response);
            lua_call(lua, 2, 0);

            lua_pushnil(lua);
            lua_setfield(lua, options, __response);
        }

        lua_pop(lua, 1); // response

        if(skip >= r)
        {
            lua_pop(lua, 3); // self + options + callback
            return;
        }
    }

    // content
    if(mod->ready_state == 2)
    {
        // Transfer-Encoding: chunked
        if(mod->is_chunked)
        {
            while(skip < r)
            {
                if(!mod->chunk_left)
                {
                    if(!http_parse_chunk(&buffer[skip], m))
                    {
                        call_error(callback, self, "invalid chunk");
                        lua_pop(lua, 3); // self + options + callback
                        mod->ready_state = 3;
                        return;
                    }

                    char cs_str[] = "00000000";
                    const size_t cs_size = m[1].eo - m[1].so;
                    const size_t cs_skip = 8 - cs_size;
                    memcpy(&cs_str[cs_skip], &buffer[skip], cs_size);

                    uint8_t cs_hex[4];
                    str_to_hex(cs_str, cs_hex, sizeof(cs_hex));
                    mod->chunk_left = (cs_hex[0] << 24)
                                    | (cs_hex[1] << 16)
                                    | (cs_hex[2] <<  8)
                                    | (cs_hex[3]      );
                    skip += m[0].eo;

                    if(!mod->chunk_left)
                    {
                        // close connection
                        call_nil(callback, self);
                        lua_pop(lua, 3); // self + options + callback
                        method_close(mod);
                        return;
                    }
                }

                const size_t r_skip = r - skip;
                lua_pushvalue(lua, callback);
                lua_pushvalue(lua, self);
                if(mod->chunk_left < r_skip)
                {
                    lua_pushlstring(lua, &buffer[skip], mod->chunk_left);
                    lua_call(lua, 2, 0);
                    skip += mod->chunk_left;
                    mod->chunk_left = 0;
                    if(buffer[skip] == '\r')
                        ++skip;
                    if(buffer[skip] == '\n')
                        ++skip;
                    else
                    {
                        call_error(callback, self, "invalid chunk");
                        lua_pop(lua, 3); // self + options + callback
                        mod->ready_state = 3;
                        return;
                    }
                }
                else
                {
                    lua_pushlstring(lua, &buffer[skip], r_skip);
                    lua_call(lua, 2, 0);
                    mod->chunk_left -= r_skip;
                    break;
                }
            }
        }

        // Content-Length
        else if(mod->is_content_length)
        {
            if(mod->chunk_left > 0)
            {
                const size_t r_skip = r - skip;
                lua_pushvalue(lua, callback);
                lua_pushvalue(lua, self);
                if(mod->chunk_left > r_skip)
                {
                    lua_pushlstring(lua, &buffer[skip], r_skip);
                    lua_call(lua, 2, 0);
                    mod->chunk_left -= r_skip;
                }
                else
                {
                    lua_pushlstring(lua, &buffer[skip], mod->chunk_left);
                    lua_call(lua, 2, 0);
                    mod->chunk_left = 0;

                    // close connection
                    call_nil(callback, self);
                    lua_pop(lua, 3); // self + options + callback
                    method_close(mod);
                    return;
                }
            }
        }

        // Stream
        else
        {
            lua_pushvalue(lua, callback);
            lua_pushvalue(lua, self);
            lua_pushlstring(lua, &buffer[skip], r - skip);
            lua_call(lua, 2, 0);
        }
    }

    lua_pop(lua, 3); // self + options + callback
} /* on_read */

static void on_connect(void *arg, int is_data)
{
    module_data_t *mod = arg;

    asc_timer_destroy(mod->timeout_timer);
    mod->timeout_timer = NULL;

    if(!is_data)
    {
        mod->is_connected = 1;
        timeout_callback(mod);
        method_close(mod);
        return;
    }

    asc_socket_event_on_read(mod->sock, on_read, mod);

    mod->is_connected = 2;
    mod->timeout_timer = asc_timer_init(REQUEST_TIMEOUT_INTERVAL, timeout_callback, mod);

    // default values
    static const char def_method[] = "GET";
    static const char def_uri[] = "/";
    static const char def_version[] = "HTTP/1.1";

    // send request
    char *buffer = mod->buffer;
    const char *buffer_tail = mod->buffer + HTTP_BUFFER_SIZE;

    lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_options);

    lua_getfield(lua, -1, __method);
    buffer_set_text(&buffer, buffer_tail - buffer
                    , def_method, sizeof(def_method) - 1, 0);
    lua_pop(lua, 1); // method

    lua_getfield(lua, -1, __uri);
    buffer_set_text(&buffer, buffer_tail - buffer
                    , def_uri, sizeof(def_uri) - 1, 0);
    lua_pop(lua, 1); // uri

    lua_getfield(lua, -1, __version);
    buffer_set_text(&buffer, buffer_tail - buffer
                    , def_version, sizeof(def_version) - 1, 1);
    lua_pop(lua, 1); // version

    lua_getfield(lua, -1, __headers);
    if(lua_type(lua, -1) == LUA_TTABLE)
    {
        for(lua_pushnil(lua); lua_next(lua, -2); lua_pop(lua, 1))
            buffer_set_text(&buffer, buffer_tail - buffer, "", 0, 1);
    }
    lua_pop(lua, 1); // headers

    lua_pushnil(lua);
    buffer_set_text(&buffer, buffer_tail - buffer, "", 0, 1);
    lua_pop(lua, 1); // empty line

    const int header_size = buffer - mod->buffer;
    if(asc_socket_send(mod->sock, mod->buffer, header_size) != header_size)
    {
        // TODO: check result
    }

    // send request content
    lua_getfield(lua, -1, __content);
    if(!lua_isnil(lua, -1))
    {
        const int content_size = luaL_len(lua, -1);
        const char *content = lua_tostring(lua, -1);
        if(asc_socket_send(mod->sock, (void *)content, content_size) != content_size)
        {
            // TODO: check result
        }
    }
    lua_pop(lua, 1); // content

    lua_pop(lua, 1); // options
} /* on_connect */

/* methods */

static int method_close(module_data_t *mod)
{
    if(mod->timeout_timer)
    {
        asc_timer_destroy(mod->timeout_timer);
        mod->timeout_timer = NULL;
    }

    if(mod->sock)
    {
        asc_socket_close(mod->sock);
        mod->sock = NULL;
    }

    if(mod->idx_self > 0)
    {
        luaL_unref(lua, LUA_REGISTRYINDEX, mod->idx_self);
        mod->idx_self = 0;
    }

    if(mod->idx_options > 0)
    {
        luaL_unref(lua, LUA_REGISTRYINDEX, mod->idx_options);
        mod->idx_options = 0;
    }

    return 0;
}

static int method_send(module_data_t *mod)
{
    if(mod->ready_state == 2)
    {
        const int content_size = luaL_len(lua, 2);
        const char *content = lua_tostring(lua, 2);
        if(asc_socket_send(mod->sock, (void *)content, content_size) != content_size)
        {
            // TODO: check result
        }
        return 0;
    }

    // TODO: save for use in future

    return 0;
}

/* required */

static void module_init(module_data_t *mod)
{
    if(!module_option_string("addr", &mod->addr) || !mod->addr)
    {
        asc_log_error(MSG("option 'addr' is required"));
        astra_abort();
    }

    if(!module_option_number("port", &mod->port))
        mod->port = 80;

    lua_getfield(lua, 2, "callback");
    if(lua_type(lua, -1) != LUA_TFUNCTION)
    {
        asc_log_error(MSG("option 'callback' is required"));
        astra_abort();
    }
    lua_pop(lua, 1);

    // store self in registry
    lua_pushvalue(lua, 3);
    mod->idx_self = luaL_ref(lua, LUA_REGISTRYINDEX);
    lua_pushvalue(lua, 2);
    mod->idx_options = luaL_ref(lua, LUA_REGISTRYINDEX);

    mod->sock = asc_socket_open_tcp4();
    if(mod->sock && asc_socket_connect(mod->sock, mod->addr, mod->port))
    {
        mod->timeout_timer = asc_timer_init(CONNECT_TIMEOUT_INTERVAL, timeout_callback, mod);
        asc_socket_event_on_connect(mod->sock, on_connect, mod);
    }
    else
        method_close(mod);
}

static void module_destroy(module_data_t *mod)
{
    method_close(mod);
}

MODULE_LUA_METHODS()
{
    { "close", method_close },
    { "send", method_send }
};

MODULE_LUA_REGISTER(http_request)
