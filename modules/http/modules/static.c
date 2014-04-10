/*
 * Astra Module: HTTP Module: Static files
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
#include <fcntl.h>
#include "../http.h"

struct module_data_t
{
    const char *path;
    int skip;
    int buffer_size;
};

struct http_response_t
{
    int fd;

    size_t file_skip;
    size_t file_size;
};

static void on_ready_send_file(void *arg)
{
    http_client_t *client = arg;

    const ssize_t len = pread(  client->response->fd
                              , client->buffer, HTTP_BUFFER_SIZE
                              , client->response->file_skip);
    if(len <= 0)
    {
        http_client_error(client, "failed to read file [%s]", strerror(errno));
        http_client_close(client);
        return;
    }

    const ssize_t send_size = asc_socket_send(client->sock, client->buffer, len);
    if(send_size <= 0)
    {
        http_client_error(client, "failed to send file to client [%s]", asc_socket_error());
        http_client_close(client);
        return;
    }
    client->response->file_skip += send_size;
    client->response->file_size -= send_size;

    if(client->response->file_size == 0)
        http_client_close(client);
}

/* Stack: 1 - instance, 2 - server, 3 - client, 4 - request */
static int module_call(module_data_t *mod)
{
    http_client_t *client = lua_touserdata(lua, 3);

    if(lua_isnil(lua, 4))
    {
        if(client->response)
        {
            close(client->response->fd);
            free(client->response);
            client->response = NULL;
        }
        return 0;
    }

    client->response = calloc(1, sizeof(http_response_t));
    client->on_send = NULL;
    client->on_read = NULL;
    client->on_ready = on_ready_send_file;

    char *filename = malloc(PATH_MAX);
    sprintf(filename, "%s%s", mod->path, &client->path[mod->skip]);
    client->response->fd = open(filename, O_RDONLY);
    free(filename);
    if(client->response->fd == -1)
    {
        http_client_warning(client, "file not found %s", client->path);

        free(client->response);
        client->response = NULL;

        http_client_abort(client, 404, NULL);
        return 0;
    }

    struct stat sb;
    fstat(client->response->fd, &sb);
    client->response->file_size = sb.st_size;

    http_response_code(client, 200, NULL);
    http_response_header(client, "Content-Length: %lu", client->response->file_size);
    // TODO: mime-type
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
    static const char __path[] = "path";
    lua_getfield(lua, MODULE_OPTIONS_IDX, __path);
    asc_assert(lua_isstring(lua, -1), "[http_static] option 'path' is required");
    mod->path = lua_tostring(lua, -1);
    int path_size = luaL_len(lua, -1);
    lua_pop(lua, 1);
    // remove trailing slash
    if(mod->path[path_size - 1] == '/')
    {
        lua_pushlstring(lua, mod->path, path_size - 1);
        mod->path = lua_tostring(lua, -1);
        lua_setfield(lua, MODULE_OPTIONS_IDX, __path);
    }

    lua_getfield(lua, MODULE_OPTIONS_IDX, "skip");
    if(lua_isstring(lua, -1))
        mod->skip = luaL_len(lua, -1);
    lua_pop(lua, 1);

    struct stat s;
    asc_assert(stat(mod->path, &s) != -1, "[http_static] path is not found");
    asc_assert(S_ISDIR(s.st_mode), "[http_static] path is not directory");

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

MODULE_LUA_REGISTER(http_static)
