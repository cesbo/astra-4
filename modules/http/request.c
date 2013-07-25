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
 *      ts          - true to push data to upstream instead of callback [optional, default : false]
 *      host        - string, server hostname or IP address
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
    MODULE_LUA_DATA();
    MODULE_STREAM_DATA();

    const char *host;
    int port;

    asc_socket_t *sock;

    asc_timer_t *timeout_timer;
    int is_connected; // for timeout message

    int is_ts; /* Using TS (true) or send data to callback (false) */

    int idx_self;
    int idx_data;

    int ready_state;  // 0 - response, 1 - headers, 2 - content
    int ts_len_in_buf;// useful ts bytes in TS buffer

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
static const char __ts[] = "ts";
static const char __code[] = "code";
static const char __message[] = "message";
static const char __response[] = "__response";

static const char __content_length[] = "Content-Length: ";
static const char __transfer_encoding[] = "Transfer-Encoding: ";
static const char __connection[] = "Connection: ";

static const char __chunked[] = "chunked";
static const char __close[] = "close";
static const char __keep_alive[] = "keep-alive";

static int method_close(module_data_t *);

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

static void call_nil(module_data_t *mod)
{
    get_lua_callback(mod);
    lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_self);

    lua_pushnil(lua);

    lua_call(lua, 2, 0);
}

void timeout_callback(void *arg)
{
    module_data_t *mod = arg;

    asc_timer_destroy(mod->timeout_timer);
    mod->timeout_timer = NULL;

    switch(mod->is_connected)
    {
        case 0:
            call_error(mod, "connection timeout");
            break;
        case 1:
            call_error(mod, "connection failed");
            break;
        case 2:
            call_error(mod, "response timeout");
            break;
        default:
            call_error(mod, "unknown error");
            break;
    }

    method_close(mod);
}

/*
 * oooooooooo  ooooooooooo      o      ooooooooo
 *  888    888  888    88      888      888    88o
 *  888oooo88   888ooo8       8  88     888    888
 *  888  88o    888    oo    8oooo88    888    888
 * o888o  88o8 o888ooo8888 o88o  o888o o888ooo88
 *
 */

static void on_close(void *arg)
{
    module_data_t *mod = arg;

    if(mod->timeout_timer)
    {
        asc_timer_destroy(mod->timeout_timer);
        mod->timeout_timer = NULL;
    }

    call_nil(mod);
    method_close(mod);
}

static void on_read(void *arg)
{
    module_data_t *mod = arg;

    asc_timer_destroy(mod->timeout_timer);
    mod->timeout_timer = NULL;

    char *buffer = mod->buffer;

    int skip = mod->ts_len_in_buf;
    int r = asc_socket_recv(mod->sock, buffer + skip, HTTP_BUFFER_SIZE - skip);
    if(r <= 0)
    {
        call_nil(mod);
        method_close(mod);
        return;
    }
    r += mod->ts_len_in_buf;// Imagine that we've received more (+ previous part)

    int response = 0;

    parse_match_t m[4];

    // parse response
    if(mod->ready_state == 0)
    {
        if(!http_parse_response(buffer, m))
        {
            call_error(mod, "invalid response");
            method_close(mod);
            return;
        }

        lua_newtable(lua);
        response = lua_gettop(lua);

        lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->__lua.oref);
        lua_pushvalue(lua, -2); // duplicate table
        lua_setfield(lua, -2, __response);
        lua_pop(lua, 1); // options

        lua_pushnumber(lua, atoi(&buffer[m[2].so]));
        lua_setfield(lua, response, __code);
        lua_pushlstring(lua, &buffer[m[3].so], m[3].eo - m[3].so);
        lua_setfield(lua, response, __message);

        skip = m[0].eo;
        mod->ready_state = 1;

        if(skip >= r)
        {
            lua_pop(lua, 1); // response
            return;
        }
    }

    // parse headers
    if(mod->ready_state == 1)
    {
        if(!response)
        {
            lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->__lua.oref);
            lua_getfield(lua, -1, __response);
            lua_remove(lua, -2);
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

            if(!strncasecmp(header, __transfer_encoding, sizeof(__transfer_encoding) - 1))
            {
                const char *val = &header[sizeof(__transfer_encoding) - 1];
                if(!strncasecmp(val, __chunked, sizeof(__chunked) - 1))
                    mod->is_chunked = 1;
            }
            else if(!strncasecmp(header, __connection, sizeof(__connection) - 1))
            {
                const char *val = &header[sizeof(__connection) - 1];
                if(!strncasecmp(val, __close, sizeof(__close) - 1))
                    mod->is_close = 1;
                else if(!strncasecmp(val, __keep_alive, sizeof(__keep_alive) - 1))
                    mod->is_keep_alive = 1;
            }
            else if(!strncasecmp(header, __content_length, sizeof(__content_length) - 1))
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
            get_lua_callback(mod);
            lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_self);
            lua_pushvalue(lua, response);
            lua_call(lua, 2, 0);

            lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->__lua.oref);
            lua_pushnil(lua);
            lua_setfield(lua, -2, __response);
            lua_pop(lua, 1); // options
        }

        lua_pop(lua, 1); // response

        if(skip >= r)
            return;
    }

    // content
    if(mod->ready_state == 2)
    {
        /* Push to stream */
        if (mod->is_ts)
        {
            int pos = skip - mod->ts_len_in_buf;// buffer rewind
            while (r - pos >= TS_PACKET_SIZE)
            {
                module_stream_send(mod, (uint8_t*)&mod->buffer[pos]);
                pos += TS_PACKET_SIZE;
            }
            int left = r - pos;
            if (left > 0)
            {//there is something usefull in the end of buffer, move it to begin
                if (pos > 0) memmove(&mod->buffer[0], &mod->buffer[pos], left);
                mod->ts_len_in_buf = left;
            } else
            {//all data is processed
                mod->ts_len_in_buf = 0;
            }
        }

        // Transfer-Encoding: chunked
        else if(mod->is_chunked)
        {
            while(skip < r)
            {
                if(!mod->chunk_left)
                {
                    if(!http_parse_chunk(&buffer[skip], m))
                    {
                        call_error(mod, "invalid chunk");
                        method_close(mod);
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
                        if(mod->is_keep_alive)
                        {
                            // keep-alive connection
                            get_lua_callback(mod);
                            lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_self);
                            lua_pushstring(lua, "");
                            lua_call(lua, 2, 0);
                        }
                        else
                        {
                            // close connection
                            call_nil(mod);
                            method_close(mod);
                        }
                        return;
                    }
                }

                const size_t r_skip = r - skip;
                get_lua_callback(mod);
                lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_self);
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
                        call_error(mod, "invalid chunk");
                        method_close(mod);
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
                get_lua_callback(mod);
                lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_self);
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

                    if(mod->is_keep_alive)
                    {
                        // keep-alive connection
                        get_lua_callback(mod);
                        lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_self);
                        lua_pushstring(lua, "");
                        lua_call(lua, 2, 0);
                    }
                    else
                    {
                        // close connection
                        call_nil(mod);
                        method_close(mod);
                    }
                    return;
                }
            }
        }

        // Stream
        else
        {
            get_lua_callback(mod);
            lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_self);
            lua_pushlstring(lua, &buffer[skip], r - skip);
            lua_call(lua, 2, 0);
        }
    }
} /* on_read */

/*
 *   oooooooo8   ooooooo  oooo   oooo oooo   oooo ooooooooooo  oooooooo8 ooooooooooo
 * o888     88 o888   888o 8888o  88   8888o  88   888    88 o888     88 88  888  88
 * 888         888     888 88 888o88   88 888o88   888ooo8   888             888
 * 888o     oo 888o   o888 88   8888   88   8888   888    oo 888o     oo     888
 *  888oooo88    88ooo88  o88o    88  o88o    88  o888ooo8888 888oooo88     o888o
 *
 */

static void send_request(module_data_t *mod)
{
    if(mod->timeout_timer)
    {
        asc_timer_destroy(mod->timeout_timer);
        mod->timeout_timer = NULL;
    }
    mod->timeout_timer = asc_timer_init(REQUEST_TIMEOUT_INTERVAL, timeout_callback, mod);

    mod->ready_state = 0;

    // default values
    static const char def_method[] = "GET";
    static const char def_uri[] = "/";
    static const char def_version[] = "HTTP/1.1";

    char *buffer = mod->buffer;
    const char *buffer_tail = mod->buffer + HTTP_BUFFER_SIZE;

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
        asc_log_error(MSG("failed to send header"));
        // call error
        return;
    }

    // send request content
    lua_getfield(lua, -1, __content);
    if(!lua_isnil(lua, -1))
    {
        const int content_size = luaL_len(lua, -1);
        const char *content = lua_tostring(lua, -1);
        // TODO: send partially
        if(asc_socket_send(mod->sock, (void *)content, content_size) != content_size)
        {
            asc_log_error(MSG("failed to send content"));
        }
    }
    lua_pop(lua, 1); // content
}

static void on_connect_err(void *arg)
{
    module_data_t *mod = arg;
    mod->is_connected = 1;
    timeout_callback(mod);
    method_close(mod);
}

static void on_connect(void *arg)
{
    module_data_t *mod = arg;

    asc_socket_set_on_read(mod->sock, on_read);
    asc_socket_set_on_close(mod->sock, on_close);

    mod->is_connected = 2;

    // send request
    lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->__lua.oref);
    send_request(mod);
    lua_pop(lua, 1); // options
} /* on_connect */

/* methods */

static int method_data(module_data_t *mod)
{
    if(!mod->idx_data)
    {
        lua_newtable(lua);
        lua_pushvalue(lua, -1);
        mod->idx_data = luaL_ref(lua, LUA_REGISTRYINDEX);
    }
    else
        lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_data);

    return 1;
}

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

    if(mod->idx_data > 0)
    {
        luaL_unref(lua, LUA_REGISTRYINDEX, mod->idx_data);
        mod->idx_data = 0;
    }

    mod->ready_state = -1;

    return 0;
}

/*
 *  oooooooo8 ooooooooooo oooo   oooo ooooooooo
 * 888         888    88   8888o  88   888    88o
 *  888oooooo  888ooo8     88 888o88   888    888
 *         888 888    oo   88   8888   888    888
 * o88oooo888 o888ooo8888 o88o    88  o888ooo88
 *
 */

static int method_send(module_data_t *mod)
{
    switch(lua_type(lua, -1))
    {
        case LUA_TSTRING:
        {
            const int content_size = luaL_len(lua, 2);
            const char *content = lua_tostring(lua, 2);
            // TODO: send partially
            if(asc_socket_send(mod->sock, (void *)content, content_size) != content_size)
            {
                asc_log_error(MSG("failed to send content"));
            }
            break;
        }
        case LUA_TTABLE:
        {
            send_request(mod);
            break;
        }
        default:
            break;
    }

    return 0;
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
    module_stream_init(mod, NULL);

    if(!module_option_string("host", &mod->host) || !mod->host)
    {
        asc_log_error(MSG("option 'host' is required"));
        astra_abort();
    }

    if(!module_option_number("port", &mod->port))
        mod->port = 80;

    module_option_number("ts", &mod->is_ts);

    lua_getfield(lua, 2, __callback);
    if(lua_type(lua, -1) != LUA_TFUNCTION)
    {
        asc_log_error(MSG("option 'callback' is required"));
        astra_abort();
    }
    lua_pop(lua, 1); // callback

    // store self in registry
    lua_pushvalue(lua, 3);
    mod->idx_self = luaL_ref(lua, LUA_REGISTRYINDEX);

    mod->sock = asc_socket_open_tcp4(mod);

    if(!mod->sock)
    {
        method_close(mod);
        return;
    }
    mod->timeout_timer = asc_timer_init(CONNECT_TIMEOUT_INTERVAL, timeout_callback, mod);
    asc_socket_connect(mod->sock, mod->host, mod->port, on_connect, on_connect_err);
}

static void module_destroy(module_data_t *mod)
{
    module_stream_destroy(mod);
    method_close(mod);
}

MODULE_STREAM_METHODS()
MODULE_LUA_METHODS()
{
    MODULE_STREAM_METHODS_REF(),
    { "close", method_close },
    { "send", method_send },
    { "data", method_data }
};

MODULE_LUA_REGISTER(http_request)
