/*
 * Astra Module: HTTP Module: MPEG-TS Streaming
 * http://cesbo.com/astra
 *
 * Copyright (C) 2014, Andrey Dyldin <and@cesbo.com>
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

#include <astra.h>
#include "../http.h"

struct module_data_t
{
    int idx_callback;
};

struct http_response_t
{
    MODULE_STREAM_DATA();

    module_data_t *mod;

    bool is_socket_busy;
};

/*
 * client->mod - http_server module
 * client->response->mod - http_upstream module
 */

static void on_ready_send_ts(void *arg)
{
    http_client_t *client = arg;

    const ssize_t send_size = asc_socket_send(  client->sock
                                              , client->buffer
                                              , client->buffer_skip);
    if(send_size > 0)
    {
        client->buffer_skip -= send_size;
        if(client->buffer_skip > 0)
            memmove(client->buffer, &client->buffer[send_size], client->buffer_skip);
    }
    else if(send_size == -1)
    {
        http_client_error(client, "failed to send ts (%d bytes) [%s]"
                          , client->buffer_skip, asc_socket_error());
        http_client_close(client);
        return;
    }

    if(client->buffer_skip == 0)
    {
        if(client->response->is_socket_busy)
        {
            asc_socket_set_on_ready(client->sock, NULL);
            client->response->is_socket_busy = false;
        }
    }
    else
    {
        if(!client->response->is_socket_busy)
        {
            asc_socket_set_on_ready(client->sock, on_ready_send_ts);
            client->response->is_socket_busy = true;
        }
    }
}

static void on_ts(void *arg, const uint8_t *ts)
{
    http_client_t *client = arg;

    if(client->buffer_skip > HTTP_BUFFER_SIZE - TS_PACKET_SIZE)
    {
        // overflow
        client->buffer_skip = 0;
        if(client->response->is_socket_busy)
        {
            asc_socket_set_on_ready(client->sock, NULL);
            client->response->is_socket_busy = false;
        }
        return;
    }

    memcpy(&client->buffer[client->buffer_skip], ts, TS_PACKET_SIZE);
    client->buffer_skip += TS_PACKET_SIZE;

    if(!client->response->is_socket_busy && client->buffer_skip >= HTTP_BUFFER_SIZE / 2)
        on_ready_send_ts(client);
}

static void on_upstream_send(void *arg)
{
    http_client_t *client = arg;

    if(!lua_islightuserdata(lua, 3))
    {
        http_client_abort(client, 500, ":send() client instance required");
        return;
    }

    // like module_stream_init()
    void *upstream = lua_touserdata(lua, 3);
    client->response->__stream.self = (void *)client;
    client->response->__stream.on_ts = (void (*)(module_data_t *, const uint8_t *))on_ts;
    __module_stream_init(&client->response->__stream);
    __module_stream_attach(upstream, &client->response->__stream);

    client->on_read = NULL;
    client->on_ready = NULL;

    http_response_code(client, 200, NULL);
    http_response_header(client, "Cache-Control: no-cache");
    http_response_header(client, "Pragma: no-cache");
    http_response_header(client, "Content-Type: application/octet-stream");
    http_response_header(client, "Connection: close");
    http_response_send(client);
}

static int module_call(module_data_t *mod)
{
    http_client_t *client = lua_touserdata(lua, 3);

    if(lua_isnil(lua, 4))
    {
        if(client->response)
        {
            lua_rawgeti(lua, LUA_REGISTRYINDEX, client->response->mod->idx_callback);
            lua_rawgeti(lua, LUA_REGISTRYINDEX, client->idx_server);
            lua_pushlightuserdata(lua, client);
            lua_pushnil(lua);
            lua_call(lua, 3, 0);

            module_stream_destroy(client->response);

            free(client->response);
            client->response = NULL;
        }
        return 0;
    }

    client->response = calloc(1, sizeof(http_response_t));
    client->response->mod = mod;

    client->on_send = on_upstream_send;

    lua_rawgeti(lua, LUA_REGISTRYINDEX, client->response->mod->idx_callback);
    lua_rawgeti(lua, LUA_REGISTRYINDEX, client->idx_server);
    lua_pushlightuserdata(lua, client);
    lua_rawgeti(lua, LUA_REGISTRYINDEX, client->idx_request);
    lua_call(lua, 3, 0);

    return 0;
}

static int __module_call(lua_State *L)
{
    module_data_t *mod = lua_touserdata(L, lua_upvalueindex(1));
    return module_call(mod);
}

static void module_init(module_data_t *mod)
{
    lua_getfield(lua, MODULE_OPTIONS_IDX, "callback");
    asc_assert(lua_isfunction(lua, -1), "[http_upstream] option 'callback' is required");
    mod->idx_callback = luaL_ref(lua, LUA_REGISTRYINDEX);

    // Set callback for http route
    lua_getmetatable(lua, 3);
    lua_pushlightuserdata(lua, (void *)mod);
    lua_pushcclosure(lua, __module_call, 1);
    lua_setfield(lua, -2, "__call");
    lua_pop(lua, 1);
}

static void module_destroy(module_data_t *mod)
{
    __uarg(mod);
}

MODULE_LUA_METHODS()
{
    { NULL, NULL }
};

MODULE_LUA_REGISTER(http_upstream)
