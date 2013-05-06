/*
 * Astra HTTP Server Module
 * http://cesbo.com/
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
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

#define HTTP_BUFFER_SIZE (64 * 1024)

typedef struct
{
    MODULE_STREAM_DATA();

    module_data_t *mod;

    asc_socket_t *sock;

    int idx_request;
    int idx_data;

    int ready_state; // 0 - not, 1 request, 2 - headers, 3 - content

    int is_close;
    int is_keep_alive;
    int content_length;

    FILE *src_file;

    int buffer_skip;
    char buffer[HTTP_BUFFER_SIZE];
} http_client_t;

struct module_data_t
{
    const char *addr;
    int port;

    int idx_callback;

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

static void on_read(void *arg, int is_data)
{
    http_client_t *client = arg;
    module_data_t *mod = client->mod;

    if(!is_data)
    {
        lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_callback);
        lua_pushlightuserdata(lua, client);
        lua_pushnil(lua);
        lua_call(lua, 2, 0);

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

        if(client->__stream.self)
        {
            __module_stream_destroy(&client->__stream);
            client->__stream.self = NULL;
        }

        asc_list_remove_item(mod->clients, client);
        asc_socket_close(client->sock);
        free(client);
        return;
    }

    int r = asc_socket_recv(client->sock, client->buffer, HTTP_BUFFER_SIZE);
    if(r <= 0)
    {
        if(r == -1)
            asc_log_error(MSG("failed to read a request [%s]"), strerror(errno));
        on_read(client, 0);
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
            lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_callback);
            lua_pushlightuserdata(lua, client);
            lua_newtable(lua);
            lua_pushstring(lua, "failed to parse http request");
            lua_setfield(lua, -2, "message");
            lua_call(lua, 2, 0);
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
            return;
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

            ++headers_count;
            lua_pushnumber(lua, headers_count);
            lua_pushlstring(lua, header, length);
            lua_settable(lua, headers);
            skip += m[0].eo;
        }
        lua_pop(lua, 1); // headers

        if(client->ready_state == 2)
        {
            lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_callback);
            lua_pushlightuserdata(lua, client);
            lua_pushvalue(lua, request);
            lua_call(lua, 2, 0);

            luaL_unref(lua, LUA_REGISTRYINDEX, client->idx_request);
            client->idx_request = 0;
        }

        lua_pop(lua, 1); // request

        if(skip >= r)
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
                lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_callback);
                lua_pushlightuserdata(lua, client);

                if(client->content_length > r_skip)
                {
                    lua_pushlstring(lua, &client->buffer[skip], r_skip);
                    lua_call(lua, 2, 0);
                    client->content_length -= r_skip;
                }
                else
                {
                    lua_pushlstring(lua, &client->buffer[skip], client->content_length);
                    lua_call(lua, 2, 0);
                    client->content_length = -1;

                    // content is done
                    client->ready_state = 0;
                }
            }
        }

        // Stream
        else
        {
            lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_callback);
            lua_pushlightuserdata(lua, client);
            lua_pushlstring(lua, &client->buffer[skip], r - skip);
            lua_call(lua, 2, 0);
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

static void on_write_ready(void *arg, int is_data)
{
    http_client_t *client = arg;
    module_data_t *mod = client->mod;

    if(!is_data)
    {
        on_read(client, 0);
        return;
    }

    const int read_size = fread(client->buffer, 1, HTTP_BUFFER_SIZE, client->src_file);
    if(read_size <= 0)
    {
        asc_log_error(MSG("failed to read source file [%s]"), strerror(errno));
        on_read(client, 0);
        return;
    }

    if(asc_socket_send(client->sock, client->buffer, read_size) != read_size)
    {
        asc_log_warning(MSG("failed to send file to client:%d"), asc_socket_fd(client->sock));
        on_read(client, 0);
        return;
    }

    if(feof(client->src_file))
    {
        client->src_file = NULL;
        asc_socket_event_on_read(client->sock, on_read, client);
    }
}

static void on_ts(void *arg, const uint8_t *ts)
{
    http_client_t *client = arg;
    module_data_t *mod = client->mod;

    if(client->buffer_skip >= HTTP_BUFFER_SIZE - TS_PACKET_SIZE)
    {
        const int ret = asc_socket_send(client->sock, client->buffer, client->buffer_skip);
        if(ret == client->buffer_skip)
            client->buffer_skip = 0;
        else if(ret == -1)
        {
            asc_log_warning(MSG("failed to send ts to the client [%s]"), asc_socket_error());
            on_read(client, 0);
            return;
        }
        else
        {
            if(ret > 0)
            {
                asc_log_info(MSG("move memory"));
                memmove(client->buffer, &client->buffer[ret], client->buffer_skip - ret);
                client->buffer_skip -= ret;
            }
        }
    }
    memcpy(&client->buffer[client->buffer_skip], ts, TS_PACKET_SIZE);
    client->buffer_skip += TS_PACKET_SIZE;
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

    if(lua_type(lua, 3) != LUA_TTABLE)
    {
        asc_log_error(MSG(":send() table is required"));
        astra_abort();
    }

    char *buffer = client->buffer;
    char * const buffer_tail = buffer + HTTP_BUFFER_SIZE;

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
    if(!asc_socket_send(client->sock, client->buffer, header_size))
    {
        asc_log_error(MSG("failed to send response to client:%d"), asc_socket_fd(client->sock));
        return 0;
    }

    // content
    lua_getfield(lua, 3, "content");
    if(!lua_isnil(lua, -1))
    {
        const int content_size = luaL_len(lua, -1);
        const char *content = lua_tostring(lua, -1);
        if(!asc_socket_send(client->sock, (void *)content, content_size))
        {
            asc_log_error(MSG("failed to send content to client:%d [%s]")
                          , asc_socket_fd(client->sock), strerror(errno));
            return 0;
        }
    }
    lua_pop(lua, 1);

    // file
    lua_getfield(lua, 3, "file");
    if(!lua_isnil(lua, -1))
    {
        luaL_Stream *stream = luaL_checkudata(lua, -1, LUA_FILEHANDLE);
        client->src_file = stream->f;

        asc_socket_event_on_write(client->sock, on_write_ready, client);
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

static int method_close(module_data_t *mod)
{
    // close client
    if(lua_type(lua, 2) != LUA_TLIGHTUSERDATA)
    {
        asc_log_error(MSG(":close() client instance required"));
        astra_abort();
    }

    on_read(lua_touserdata(lua, 2), 0);

    return 0;
}

static void server_close(module_data_t *mod)
{
    for(asc_list_first(mod->clients)
        ; !asc_list_eol(mod->clients)
        ; asc_list_first(mod->clients))
    {
        http_client_t *client = asc_list_data(mod->clients);
        asc_socket_close(client->sock);
        free(client);
        asc_list_remove_current(mod->clients);
    }

    asc_socket_close(mod->sock);
    mod->sock = NULL;

    if(mod->idx_callback)
    {
        luaL_unref(lua, LUA_REGISTRYINDEX, mod->idx_callback);
        mod->idx_callback = 0;
    }

    asc_list_destroy(mod->clients);
    mod->clients = NULL;
}

static void on_accept(void *arg, int is_data)
{
    module_data_t *mod = arg;

    if(!is_data)
    {
        server_close(mod);
        return;
    }

    http_client_t *client = calloc(1, sizeof(http_client_t));
    if(!asc_socket_accept(mod->sock, &client->sock))
    {
        free(client);
        on_accept(mod, 0);
        return;
    }
    client->mod = mod;
    asc_list_insert_tail(mod->clients, client);

    asc_socket_event_on_read(client->sock, on_read, client);

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
    if(!module_option_string("addr", &mod->addr))
        mod->addr = "0.0.0.0";
    if(!module_option_number("port", &mod->port))
        mod->port = 80;

    // store callback in registry
    lua_getfield(lua, 2, "callback");
    if(lua_type(lua, -1) != LUA_TFUNCTION)
    {
        asc_log_error(MSG("option 'callback' is required"));
        astra_abort();
    }
    mod->idx_callback = luaL_ref(lua, LUA_REGISTRYINDEX);

    mod->clients = asc_list_init();

    mod->sock = asc_socket_open_tcp4();
    asc_socket_set_reuseaddr(mod->sock, 1);
    asc_socket_set_non_delay(mod->sock, 1);
    if(!asc_socket_bind(mod->sock, mod->addr, mod->port))
    {
        server_close(mod);
        astra_abort();
    }

    asc_socket_event_on_accept(mod->sock, on_accept, mod);
}

static void module_destroy(module_data_t *mod)
{
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
