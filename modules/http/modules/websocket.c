/*
 * Astra Module: HTTP Module: WebSocket
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

/* WebSocket Frame */
#define FRAME_HEADER_SIZE 2
#define FRAME_KEY_SIZE 4
#define FRAME_SIZE8_SIZE 0
#define FRAME_SIZE16_SIZE 2
#define FRAME_SIZE64_SIZE 8

struct module_data_t
{
    int idx_callback;
};

struct http_response_t
{
    module_data_t *mod;

    uint64_t data_size;

    uint8_t frame_key[FRAME_KEY_SIZE];
    uint8_t frame_key_i;
};

/*
 * client->mod - http_server module
 * client->response->mod - http_websocket module
 */

static const char __websocket_magic[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

/* Stack: 1 - server, 2 - client, 3 - response */
static void on_websocket_send(void *arg)
{
    http_client_t *client = arg;

    const char *str = lua_tostring(lua, 3);
    const int str_size = luaL_len(lua, 3);

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
        data[2] = ((uint64_t)str_size >> 56) & 0xFF;
        data[3] = ((uint64_t)str_size >> 48) & 0xFF;
        data[4] = ((uint64_t)str_size >> 40) & 0xFF;
        data[5] = ((uint64_t)str_size >> 32) & 0xFF;
        data[6] = ((uint64_t)str_size >> 24) & 0xFF;
        data[7] = ((uint64_t)str_size >> 16) & 0xFF;
        data[8] = ((uint64_t)str_size >> 8 ) & 0xFF;
        data[9] = ((uint64_t)str_size      ) & 0xFF;

        frame_header += FRAME_SIZE64_SIZE;
    }

    memcpy(&data[frame_header], str, str_size);

    ssize_t size = asc_socket_send(client->sock, (void *)data, frame_header + str_size);
    if(size <= 0)
    {
        http_client_error(client, "failed to send data [%s]", asc_socket_error());
        http_client_close(client);
    }
}

static void on_websocket_read(void *arg)
{
    http_client_t *client = arg;
    http_response_t *response = client->response;

    ssize_t size = asc_socket_recv(client->sock, client->buffer, HTTP_BUFFER_SIZE);
    if(size <= 0)
    {
        http_client_close(client);
        return;
    }

    size_t skip = 0;
    uint8_t *data = (uint8_t *)client->buffer;

    if(response->data_size == 0)
    {
        // TODO: check FIN, OPCODE
        // const bool fin = ((data[0] & 0x80) == 0x80);

        const uint8_t opcode = data[0] & 0x0F;
        if(opcode == 0x08)
        {
            http_client_close(client);
            return;
        }

        response->frame_key_i = 0;
        response->data_size = data[1] & 0x7F;

        if(response->data_size > 127)
        {
            http_client_warning(client, "wrong websocket frame format");
            response->data_size = 0;
        }
        else if(response->data_size < 126)
        {
            skip = FRAME_HEADER_SIZE + FRAME_SIZE8_SIZE;
        }
        else if(response->data_size == 126)
        {
            response->data_size = (data[2] << 8) | data[3];

            skip = FRAME_HEADER_SIZE + FRAME_SIZE16_SIZE;
        }
        else if(response->data_size == 127)
        {
            response->data_size = (  ((uint64_t)data[2] << 56)
                                   | ((uint64_t)data[3] << 48)
                                   | ((uint64_t)data[4] << 40)
                                   | ((uint64_t)data[5] << 32)
                                   | ((uint64_t)data[6] << 24)
                                   | ((uint64_t)data[7] << 16)
                                   | ((uint64_t)data[8] << 8 )
                                   | ((uint64_t)data[9]      ));

            skip = FRAME_HEADER_SIZE + FRAME_SIZE64_SIZE;
        }

        response->frame_key_i = 0;
        memcpy(response->frame_key, &data[skip], FRAME_KEY_SIZE);

        skip += FRAME_KEY_SIZE;
    }

    const size_t data_size = size - skip;
    data = &data[skip];
    skip = 0;

    // TODO: SSE
    while(skip < data_size)
    {
        data[skip] ^= response->frame_key[response->frame_key_i];
        ++skip;
        response->frame_key_i = (response->frame_key_i + 1) % 4;
    }
    response->data_size -= skip;

    if(!client->content)
    {
        if(response->data_size == 0)
        {
            lua_rawgeti(lua, LUA_REGISTRYINDEX, response->mod->idx_callback);
            lua_rawgeti(lua, LUA_REGISTRYINDEX, client->idx_server);
            lua_pushlightuserdata(lua, client);
            lua_pushlstring(lua, (const char *)data, skip);
            lua_call(lua, 3, 0);
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

        if(response->data_size == 0)
        {
            lua_rawgeti(lua, LUA_REGISTRYINDEX, response->mod->idx_callback);
            lua_rawgeti(lua, LUA_REGISTRYINDEX, client->idx_server);
            lua_pushlightuserdata(lua, client);
            string_buffer_push(lua, client->content);
            client->content = NULL;
            lua_call(lua, 3, 0);
        }
    }
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

            if(client->content)
            {
                string_buffer_free(client->content);
                client->content = NULL;
            }
            free(client->response);
            client->response = NULL;
        }
        return 0;
    }

    lua_rawgeti(lua, LUA_REGISTRYINDEX, client->idx_request);
    lua_getfield(lua, -1, "headers");

    lua_getfield(lua, -1, "upgrade");
    if(lua_isnil(lua, -1))
    {
        lua_pop(lua, 3);
        http_client_abort(client, 400, NULL);
        return 0;
    }
    const char *upgrade = lua_tostring(lua, -1);
    if(strcmp(upgrade, "websocket") != 0)
    {
        lua_pop(lua, 3);
        http_client_abort(client, 400, NULL);
        return 0;
    }
    lua_pop(lua, 1); // upgrade

    char *accept_key = NULL;

    lua_getfield(lua, -1, "sec-websocket-key");
    if(lua_isstring(lua, -1))
    {
        const char *key = lua_tostring(lua, -1);
        const int key_size = luaL_len(lua, -1);
        sha1_ctx_t ctx;
        memset(&ctx, 0, sizeof(sha1_ctx_t));
        sha1_init(&ctx);
        sha1_update(&ctx, (const uint8_t *)key, key_size);
        sha1_update(&ctx, (const uint8_t *)__websocket_magic, sizeof(__websocket_magic) - 1);
        uint8_t digest[SHA1_DIGEST_SIZE];
        sha1_final(&ctx, digest);
        accept_key = base64_encode(digest, sizeof(digest), NULL);
    }
    lua_pop(lua, 1); // sec-websocket-key

    lua_pop(lua, 2); // request + headers

    client->response = calloc(1, sizeof(http_response_t));
    client->response->mod = mod;
    client->on_send = on_websocket_send;
    client->on_read = on_websocket_read;
    client->on_ready = NULL;

    http_response_code(client, 101, "Switching Protocols");
    http_response_header(client, "Upgrade: websocket");
    http_response_header(client, "Connection: Upgrade");
    if(accept_key)
    {
        http_response_header(client, "Sec-WebSocket-Accept: %s", accept_key);
        free(accept_key);
    }
    http_response_send(client);

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
    asc_assert(lua_isfunction(lua, -1), "[http_websocket] option 'callback' is required");
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
    if(mod->idx_callback)
    {
        luaL_unref(lua, LUA_REGISTRYINDEX, mod->idx_callback);
        mod->idx_callback = 0;
    }
}

MODULE_LUA_METHODS()
{
    { NULL, NULL }
};

MODULE_LUA_REGISTER(http_websocket)
