/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#include <astra.h>
#include "parser.h"

#include <modules/utils/utils.h>

#define LOG_MSG(_msg) "[http_request] " _msg
#define BUFFER_SIZE 4096
#define CONNECT_TIMEOUT_INTERVAL (5*1000)
#define REQUEST_TIMEOUT_INTERVAL (60*1000)

struct module_data_s
{
    MODULE_BASE();

    struct
    {
        const char *host;
        int port;
    } config;

    int sock;
    void *timeout_timer;
    int is_connected; // for timeout message

    int idx_self;

    int ready_state; // 0 - not, 1 response, 2 - headers, 3 - content

    size_t chunk_left; // is_chunked || is_content_length

    // headers
    int is_close;
    int is_keep_alive;
    int is_chunked;
    int is_content_length;

    char buffer[BUFFER_SIZE];
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

static void buffer_set_text(lua_State *L, char **buffer, int capacity
                            , const char *default_value, int default_len
                            , int is_end)
{
    // TODO: check capacity
    char *ptr = *buffer;
    if(!lua_isnil(L, -1))
    {
        default_value = lua_tostring(L, -1);
        default_len = luaL_len(L, -1);
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

    timer_detach(mod->timeout_timer);
    mod->timeout_timer = NULL;

    lua_State *L = LUA_STATE(mod);

    lua_rawgeti(L, LUA_REGISTRYINDEX, mod->idx_self);
    const int self = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, mod->__idx_options);

    // callback
    lua_getfield(L, -1, __callback);
    lua_pushvalue(L, self);
    lua_newtable(L);
    lua_pushnumber(L, 0);
    lua_setfield(L, -2, __code);
    switch(mod->is_connected)
    {
        case 0:
            lua_pushstring(L, "connection timeout");
            break;
        case 1:
            lua_pushstring(L, "connection failed");
            break;
        case 2:
            lua_pushstring(L, "response timeout");
            break;
        default:
            lua_pushstring(L, "unknown error");
            break;
    }
    lua_setfield(L, -2, __message);
    lua_call(L, 2, 0);

    lua_pop(L, 2); // options + self

    method_close(mod);
}

static void call_error(lua_State *L, int callback, int self, const char *msg)
{
    lua_pushvalue(L, callback);
    lua_pushvalue(L, self);

    lua_newtable(L);
    lua_pushnumber(L, 0);
    lua_setfield(L, -2, __code);
    lua_pushstring(L, msg);
    lua_setfield(L, -2, __message);

    lua_call(L, 2, 0);
}

static void call_nil(lua_State *L, int callback, int self)
{
    lua_pushvalue(L, callback);
    lua_pushvalue(L, self);

    lua_pushnil(L);

    lua_call(L, 2, 0);
}

static void event_callback_read(void *arg, int event)
{
    module_data_t *mod = arg;

    timer_detach(mod->timeout_timer);
    mod->timeout_timer = NULL;

    lua_State *L = LUA_STATE(mod);

    lua_rawgeti(L, LUA_REGISTRYINDEX, mod->idx_self);
    const int self = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, mod->__idx_options);
    const int options = lua_gettop(L);
    lua_getfield(L, -1, __callback);
    const int callback = lua_gettop(L);

    if(event == EVENT_ERROR)
    {
        call_nil(L, callback, self);
        lua_pop(L, 3); // self + options + callback
        method_close(mod);
        return;
    }

    char *buffer = mod->buffer;
    ssize_t r = socket_recv(mod->sock, buffer, BUFFER_SIZE);
    if(r <= 0)
    {
        call_nil(L, callback, self);
        lua_pop(L, 3); // self + options + callback
        method_close(mod);
        return;
    }

    int response = 0;
    size_t skip = 0;

    parse_match_t m[4];

    // parse response
    if(mod->ready_state == 0)
    {
        if(!http_parse_response(buffer, m))
        {
            call_error(L, callback, self, "invalid response");
            lua_pop(L, 3); // self + options + callback
            return;
        }

        lua_newtable(L);
        response = lua_gettop(L);
        lua_pushvalue(L, -1); // duplicate table
        lua_setfield(L, options, __response);

        lua_pushnumber(L, atoi(&buffer[m[2].so]));
        lua_setfield(L, response, __code);
        lua_pushlstring(L, &buffer[m[3].so], m[3].eo - m[3].so);
        lua_setfield(L, response, __message);

        skip = m[0].eo;
        mod->ready_state = 1;

        if(skip >= r)
        {
            lua_pop(L, 4); // self + options + callback + response
            return;
        }
    }

    // parse headers
    if(mod->ready_state == 1)
    {
        if(!response)
        {
            lua_getfield(L, options, __response);
            response = lua_gettop(L);
        }

        int headers_count = 0;
        lua_getfield(L, response, __headers);
        if(lua_isnil(L, -1))
        {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_pushvalue(L, -1);
            lua_setfield(L, response, __headers);
        }
        else
        {
            headers_count = luaL_len(L, -1);
        }
        const int headers = lua_gettop(L);

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
            lua_pushnumber(L, headers_count);
            lua_pushlstring(L, header, length);
            lua_settable(L, headers);
            skip += m[0].eo;
        }

        lua_pop(L, 1); // headers

        if(mod->ready_state == 2)
        {
            lua_pushvalue(L, callback);
            lua_pushvalue(L, self);
            lua_pushvalue(L, response);
            lua_call(L, 2, 0);

            lua_pushnil(L);
            lua_setfield(L, options, __response);
        }

        lua_pop(L, 1); // response

        if(skip >= r)
        {
            lua_pop(L, 3); // self + options + callback
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
                        call_error(L, callback, self, "invalid chunk");
                        lua_pop(L, 3); // self + options + callback
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
                        call_nil(L, callback, self);
                        lua_pop(L, 3); // self + options + callback
                        method_close(mod);
                        return;
                    }
                }

                const size_t r_skip = r - skip;
                lua_pushvalue(L, callback);
                lua_pushvalue(L, self);
                if(mod->chunk_left < r_skip)
                {
                    lua_pushlstring(L, &buffer[skip], mod->chunk_left);
                    lua_call(L, 2, 0);
                    skip += mod->chunk_left;
                    mod->chunk_left = 0;
                    if(buffer[skip] == '\r')
                        ++skip;
                    if(buffer[skip] == '\n')
                        ++skip;
                    else
                    {
                        call_error(L, callback, self, "invalid chunk");
                        lua_pop(L, 3); // self + options + callback
                        mod->ready_state = 3;
                        return;
                    }
                }
                else
                {
                    lua_pushlstring(L, &buffer[skip], r_skip);
                    lua_call(L, 2, 0);
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
                lua_pushvalue(L, callback);
                lua_pushvalue(L, self);
                if(mod->chunk_left > r_skip)
                {
                    lua_pushlstring(L, &buffer[skip], r_skip);
                    lua_call(L, 2, 0);
                    mod->chunk_left -= r_skip;
                }
                else
                {
                    lua_pushlstring(L, &buffer[skip], mod->chunk_left);
                    lua_call(L, 2, 0);
                    mod->chunk_left = 0;

                    // close connection
                    call_nil(L, callback, self);
                    lua_pop(L, 3); // self + options + callback
                    method_close(mod);
                    return;
                }
            }
        }

        // Stream
        else
        {
            lua_pushvalue(L, callback);
            lua_pushvalue(L, self);
            lua_pushlstring(L, &buffer[skip], r - skip);
            lua_call(L, 2, 0);
        }
    }

    lua_pop(L, 3); // self + options + callback
} /* event_callback_read */

static void event_callback_write(void *arg, int event)
{
    module_data_t *mod = arg;

    timer_detach(mod->timeout_timer);
    mod->timeout_timer = NULL;

    if(event == EVENT_ERROR)
    {
        mod->is_connected = 1;
        timeout_callback(mod);
        method_close(mod);
        return;
    }

    event_detach(mod->sock);
    event_attach(mod->sock, event_callback_read, mod, EVENT_READ);

    mod->is_connected = 2;
    mod->timeout_timer
        = timer_attach(REQUEST_TIMEOUT_INTERVAL, timeout_callback, mod);

    // default values
    static const char def_method[] = "GET";
    static const char def_uri[] = "/";
    static const char def_version[] = "HTTP/1.1";

    // send request
    char *buffer = mod->buffer;
    const char *buffer_tail = mod->buffer + BUFFER_SIZE;

    lua_State *L = LUA_STATE(mod);
    lua_rawgeti(L, LUA_REGISTRYINDEX, mod->__idx_options);

    lua_getfield(L, -1, __method);
    buffer_set_text(L, &buffer, buffer_tail - buffer
                    , def_method, sizeof(def_method) - 1, 0);
    lua_pop(L, 1); // method

    lua_getfield(L, -1, __uri);
    buffer_set_text(L, &buffer, buffer_tail - buffer
                    , def_uri, sizeof(def_uri) - 1, 0);
    lua_pop(L, 1); // uri

    lua_getfield(L, -1, __version);
    buffer_set_text(L, &buffer, buffer_tail - buffer
                    , def_version, sizeof(def_version) - 1, 1);
    lua_pop(L, 1); // version

    lua_getfield(L, -1, __headers);
    if(lua_type(L, -1) == LUA_TTABLE)
    {
        for(lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1))
            buffer_set_text(L, &buffer, buffer_tail - buffer, "", 0, 1);
    }
    lua_pop(L, 1); // headers

    lua_pushnil(L);
    buffer_set_text(L, &buffer, buffer_tail - buffer, "", 0, 1);
    lua_pop(L, 1); // empty line

    const size_t header_size = buffer - mod->buffer;
    socket_send(mod->sock, mod->buffer, header_size);
    // TODO: check result

    // send request content
    lua_getfield(L, -1, __content);
    if(!lua_isnil(L, -1))
    {
        const int content_size = luaL_len(L, -1);
        const char *content = lua_tostring(L, -1);
        socket_send(mod->sock, (void *)content, content_size);
    }
    lua_pop(L, 1); // content

    lua_pop(L, 1); // options
} /* event_callback_write */

/* methods */

static int method_close(module_data_t *mod)
{
    if(mod->timeout_timer)
    {
        timer_detach(mod->timeout_timer);
        mod->timeout_timer = NULL;
    }

    if(mod->sock > 0)
    {
        event_detach(mod->sock);
        socket_close(mod->sock);
        mod->sock = 0;
    }

    if(mod->idx_self > 0)
    {
        lua_State *L = LUA_STATE(mod);
        luaL_unref(L, LUA_REGISTRYINDEX, mod->idx_self);
        mod->idx_self = 0;
    }

    return 0;
}

static int method_send(module_data_t *mod)
{
    lua_State *L = LUA_STATE(mod);

    if(mod->ready_state == 2)
    {
        const int content_size = luaL_len(L, 2);
        const char *content = lua_tostring(L, 2);
        socket_send(mod->sock, (void *)content, content_size);
        return 0;
    }

    // TODO: save for use in future

    return 0;
}

/* required */

static void module_configure(module_data_t *mod)
{
    /*
     * OPTIONS:
     *   host, port, callback,
     *   method, uri, version, headers, content
     */

    module_set_string(mod, "host", 1, NULL, &mod->config.host);
    module_set_number(mod, "port", 0, 80, &mod->config.port);

    lua_State *L = LUA_STATE(mod);
    lua_getfield(L, 2, "callback");
    if(lua_type(L, -1) != LUA_TFUNCTION)
    {
        log_error(LOG_MSG("option \"callback\" is required"));
        abort();
    }
    lua_pop(L, 1);
}

static void module_initialize(module_data_t *mod)
{
    module_configure(mod);

    lua_State *L = LUA_STATE(mod);

    // store self in registry
    lua_pushvalue(L, -1);
    mod->idx_self = luaL_ref(L, LUA_REGISTRYINDEX);

    mod->sock = socket_open(SOCKET_CONNECT
                            , mod->config.host, mod->config.port);
    if(mod->sock)
    {
        mod->timeout_timer
            = timer_attach(CONNECT_TIMEOUT_INTERVAL, timeout_callback, mod);
        event_attach(mod->sock, event_callback_write, mod, EVENT_WRITE);
    }
    else
        method_close(mod);
}

static void module_destroy(module_data_t *mod)
{
    method_close(mod);
}

MODULE_METHODS()
{
    METHOD(close)
    METHOD(send)
};

MODULE(http_request)
