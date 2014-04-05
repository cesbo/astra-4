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
 *      callback    - function,
 *
 * Module Methods:
 *      port()      - return number, server port
 *      close()     - close server
 *      close(client)
 *                  - close client connection
 *      send(client, response)
 *                  - stream response to client. response - table:
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
#include "parser.h"

#define MSG(_msg) "[http_server %s:%d] " _msg, mod->addr, mod->port
#define HTTP_BUFFER_SIZE 8192

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
    const char *boundary;

    size_t chunk_left; // is_content_length

    string_buffer_t *content;
} http_client_t;

struct module_data_t
{
    MODULE_LUA_DATA();

    int idx_self;

    const char *addr;
    int port;

    asc_socket_t *sock;
    asc_list_t *clients;
};

static const char __method[] = "method";
static const char __version[] = "version";
static const char __path[] = "path";
static const char __query[] = "query";
static const char __headers[] = "headers";
static const char __content[] = "content";

static const char __content_length[] = "Content-Length: ";

static const char __content_type[] = "Content-Type: ";
static const char __multipart[] = "multipart";
// TODO: static const char __boundary[] = "boundary";
static const char __json[] = "application/json";
static const char __urlencoded[] = "application/x-www-form-urlencoded";

static const char __connection[] = "Connection: ";
static const char __close[] = "close";
static const char __keep_alive[] = "keep-alive";

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
    const int response = lua_gettop(lua);
    lua_rawgeti(lua, LUA_REGISTRYINDEX, client->mod->__lua.oref);
    lua_getfield(lua, -1, "callback");
    lua_rawgeti(lua, LUA_REGISTRYINDEX, client->mod->idx_self);
    lua_pushlightuserdata(lua, client);
    lua_pushvalue(lua, response);
    lua_call(lua, 3, 0);
    lua_pop(lua, 2); // oref + response
}

static void on_client_close(void *arg)
{
    http_client_t *client = arg;
    module_data_t *mod = client->mod;

    printf(MSG("%s(): %p\n"), __FUNCTION__, (void *)client);

    if(!client->sock)
        return;

    asc_socket_close(client->sock);
    client->sock = NULL;

    if(client->is_request_ok)
    {
        lua_pushnil(lua);
        callback(client);
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
        // TODO: && not websocket && is debug
        asc_log_warning(MSG("received data after request"));
        return;
    }

    parse_match_t m[4];

    // request line
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
                lua_pop(lua, 2); // query + response
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

        lua_pop(lua, 1); // response

        if(skip >= size)
            return;
    }

    // request headers
    if(!client->is_request_headers)
    {
        lua_rawgeti(lua, LUA_REGISTRYINDEX, client->idx_request);
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
                }
                else if(!strncasecmp(val, __json, sizeof(__json) - 1))
                {
                    client->is_json = true;
                }
                else if(!strncasecmp(val, __urlencoded, sizeof(__urlencoded) - 1))
                {
                    client->is_urlencoded = true;
                }
            }

            ++headers_count;
            lua_pushnumber(lua, headers_count);
            lua_pushlstring(lua, header, length);
            lua_settable(lua, headers);
            skip += m[0].eo;
        }

        lua_pop(lua, 2); // headers + response

        if(client->is_request_headers)
        {
            if(!client->is_content)
            {
                client->is_request_ok = true;
                lua_rawgeti(lua, LUA_REGISTRYINDEX, client->idx_request);
                callback(client);
            }
        }

        if(skip >= size)
            return;
    }

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
                lua_call(lua, 1, 1);
            }
            else if(client->is_urlencoded)
            {
                string_buffer_push(lua, client->content);
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
            }

            client->is_request_ok = true;
            client->content = NULL;
            lua_setfield(lua, -2, __content);
            callback(client);
        }

        return;
    }
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

    printf(MSG("%s(): %p\n"), __FUNCTION__, (void *)client);
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

    // store callback in registry
    lua_getfield(lua, 2, "callback");
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
    { "close", method_close },
    { "data", method_data }
};

MODULE_LUA_REGISTER(http_server)
