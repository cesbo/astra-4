/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#include <astra.h>
#include "parser.h"
#include <modules/mpegts/mpegts.h>

#define LOG_MSG(_msg) "[http_client %s:%d] " _msg \
        , mod->config.addr, mod->config.port
#define BUFFER_SIZE 4096
#define TS_BUFFER_SIZE 0x10000

struct module_data_s
{
    MODULE_BASE();

    struct
    {
        const char *addr;
        int port;
    } config;

    int idx_self;

    int sock;

    int ready_state; // 0 - not, 1 request, 2 - headers, 3 - content

    size_t chunk_left; // is_content_length

    // headers
    int is_close;
    int is_keep_alive;
    int is_content_length;

    char buffer[BUFFER_SIZE];

    size_t ts_buffer_skip;
    uint8_t *ts_buffer;
};

static const char __version[] = "version";
static const char __code[] = "code";
static const char __message[] = "message";
static const char __headers[] = "headers";
static const char __content[] = "content";
static const char __callback[] = "callback";
static const char __method[] = "method";
static const char __uri[] = "uri";

static const char __request[] = "__request";

static const char __content_length[] = "Content-Length: ";
static const char __connection[] = "Connection: ";

static const char __close[] = "close";
static const char __keep_alive[] = "keep-alive";

/* callbacks */

static int method_close(module_data_t *);

static void call_error(lua_State *L, int callback, int self, const char *msg)
{
    lua_pushvalue(L, callback);
    lua_pushvalue(L, self);

    lua_newtable(L);
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

static void client_callback(void *arg, int event)
{
    module_data_t *mod = arg;

    if(!mod->idx_self)
        return;

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

    int request = 0;
    size_t skip = 0;

    parse_match_t m[4];

    // parse request
    if(mod->ready_state == 0)
    {
        if(!http_parse_request(buffer, m))
        {
            call_error(L, callback, self, "failed to parse http request");
            lua_pop(L, 3); // self + options + callback
            return;
        }

        lua_newtable(L);
        request = lua_gettop(L);
        lua_pushvalue(L, -1); // duplicate table
        lua_setfield(L, options, __request);

        lua_pushlstring(L, &buffer[m[1].so], m[1].eo - m[1].so);
        lua_setfield(L, request, __method);
        lua_pushlstring(L, &buffer[m[2].so], m[2].eo - m[2].so);
        lua_setfield(L, request, __uri);
        lua_pushlstring(L, &buffer[m[3].so], m[3].eo - m[3].so);
        lua_setfield(L, request, __version);

        skip = m[0].eo;
        mod->ready_state = 1;

        if(skip >= r)
        {
            lua_pop(L, 4); // self + options + callback + request
            return;
        }
    }

    // parse headers
    if(mod->ready_state == 1)
    {
        if(!request)
        {
            lua_getfield(L, options, __request);
            request = lua_gettop(L);
        }

        int headers_count = 0;
        lua_getfield(L, request, __headers);
        if(lua_isnil(L, -1))
        {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_pushvalue(L, -1);
            lua_setfield(L, request, __headers);
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

            if(!strncasecmp(header, __connection
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
            lua_pushvalue(L, request);
            lua_call(L, 2, 0);

            lua_pushnil(L);
            lua_setfield(L, options, __request);
        }

        lua_pop(L, 1); // request

        if(skip >= r)
        {
            lua_pop(L, 3); // self + options + callback
            return;
        }
    }

    // content
    if(mod->ready_state == 2)
    {
        // Content-Length
        if(mod->is_content_length)
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

                    // content is done
                    mod->ready_state = 0;
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
} /* client_callback */

static void callback_send_ts(module_data_t *mod, uint8_t *ts)
{
    if(mod->ts_buffer_skip > TS_BUFFER_SIZE - TS_PACKET_SIZE)
    {
        if(mod->ts_buffer_skip == 0)
            return;
        socket_send(mod->sock, mod->ts_buffer, mod->ts_buffer_skip);
        mod->ts_buffer_skip = 0;
    }
    memcpy(&mod->ts_buffer[mod->ts_buffer_skip], ts, TS_PACKET_SIZE);
    mod->ts_buffer_skip += TS_PACKET_SIZE;
}

static void callback_on_attach(module_data_t *mod, module_data_t *parent)
{
    if(!mod->ts_buffer)
        mod->ts_buffer = malloc(TS_BUFFER_SIZE);
}

/* methods */

static int method_close(module_data_t *mod)
{
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

static int method_send(module_data_t *mod)
{
    lua_State *L = LUA_STATE(mod);

    const int type_2 = lua_type(L, 2);
    if(type_2 == LUA_TSTRING)
    {
        const int content_size = luaL_len(L, 2);
        const char *content = lua_tostring(L, 2);
        socket_send(mod->sock, (void *)content, content_size);
        return 0;
    }
    else if(type_2 != LUA_TTABLE)
    {
        log_error(LOG_MSG("wrong parameter type [%d]"), type_2);
        return 0;
    }

    char *buffer = mod->buffer;
    char * const buffer_tail = buffer + BUFFER_SIZE;

    lua_getfield(L, 2, __version);
    static const char d_version[] = "HTTP/1.1";
    buffer_set_text(L, &buffer, buffer_tail - buffer
                    , d_version, sizeof(d_version) - 1, 0);
    lua_pop(L, 1); // version

    lua_getfield(L, 2, __code);
    static const char d_code[] = "200";
    buffer_set_text(L, &buffer, buffer_tail - buffer
                    , d_code, sizeof(d_code) - 1, 0);
    lua_pop(L, 1); // code

    lua_getfield(L, 2, __message);
    static const char d_message[] = "OK";
    buffer_set_text(L, &buffer, buffer_tail - buffer
                    , d_message, sizeof(d_message) - 1, 1);
    lua_pop(L, 1); // message

    lua_getfield(L, 2, __headers);
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

    lua_getfield(L, 2, __content);
    if(!lua_isnil(L, -1))
    {
        const int content_size = luaL_len(L, -1);
        const char *content = lua_tostring(L, -1);
        socket_send(mod->sock, (void *)content, content_size);
    }
    lua_pop(L, 1); // content

    return 0;
}

/* required */

static void module_initialize(module_data_t *mod)
{
    /*
     * OPTIONS:
     *   fd, addr, port
     *   server, callback
     */
    module_set_number(mod, "fd", 1, 0, &mod->sock);
    module_set_string(mod, "addr", 1, NULL, &mod->config.addr);
    module_set_number(mod, "port", 1, 0, &mod->config.port);

    stream_ts_init(mod, callback_send_ts, callback_on_attach
                   , NULL, NULL, NULL);

    lua_State *L = LUA_STATE(mod);

    // store self in registry
    lua_pushvalue(L, -1);
    mod->idx_self = luaL_ref(L, LUA_REGISTRYINDEX);

    event_attach(mod->sock, client_callback, mod, EVENT_READ);
    log_debug(LOG_MSG("fd=%d"), mod->sock);
}

static void module_destroy(module_data_t *mod)
{
    stream_ts_destroy(mod);

    if(mod->ts_buffer)
    {
        free(mod->ts_buffer);
        mod->ts_buffer = NULL;
    }

    method_close(mod);
}

MODULE_METHODS()
{
    METHOD(send)
    METHOD(close)
};

MODULE(http_client)
