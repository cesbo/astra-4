/*
 * Astra Module: HTTP Module: Static files
 * http://cesbo.com/astra
 *
 * Copyright (C) 2014-2015, Andrey Dyldin <and@cesbo.com>
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

#if defined(__linux) || defined(__APPLE__) || defined(__FreeBSD__)
#   define ASC_SENDFILE (128 * 1024)
#endif

#ifdef ASC_SENDFILE
#   include <sys/socket.h>
#   ifdef __linux
#       include <sys/sendfile.h>
#   endif
#endif

#include "../http.h"

struct module_data_t
{
    const char *path;
    int path_skip;

    size_t block_size;

    const char *default_mime;
};

struct http_response_t
{
    module_data_t *mod;

    int file_fd;
    int sock_fd;

    off_t file_skip;
    off_t file_size;
};

static const char __path[] = "path";

/*
 * client->mod - http_server module
 * client->response->mod - http_static module
 */

static void on_ready_send_file(void *arg)
{
    http_client_t *client = (http_client_t *)arg;
    http_response_t *response = client->response;

    ssize_t send_size;

    if(!response->mod->block_size)
    {
        const ssize_t len = pread(  response->file_fd
                                  , client->buffer, HTTP_BUFFER_SIZE
                                  , response->file_skip);
        if(len <= 0)
            send_size = -1;
        else
            send_size = asc_socket_send(client->sock, client->buffer, len);
    }
    else
    {
#if defined(__linux)

        send_size = sendfile(  response->sock_fd
                             , response->file_fd
                             , NULL, response->mod->block_size);

#elif defined(__APPLE__)

        off_t block_size = response->mod->block_size;
        const int r = sendfile(  response->file_fd
                               , response->sock_fd
                               , response->file_skip
                               , &block_size, NULL, 0);

        if(r == 0 || (r == -1 && errno == EAGAIN && block_size > 0))
            send_size = block_size;
        else
            send_size = -1;

#elif defined(__FreeBSD__)

        off_t block_size = 0;
        const int r = sendfile(  response->file_fd
                               , response->sock_fd
                               , response->file_skip
                               , response->mod->block_size, NULL
                               , &block_size, 0);

        if(r == 0 || (r == -1 && errno == EAGAIN && block_size > 0))
            send_size = block_size;
        else
            send_size = -1;

#else

        send_size = -1;

#endif
    }

    if(send_size == -1)
    {
        http_client_error(client, "failed to send file [%s]", asc_socket_error());
        http_client_close(client);
        return;
    }

    response->file_skip += send_size;

    if(response->file_skip >= response->file_size)
        http_client_close(client);
}

static const char * lua_get_mime(http_client_t *client, const char *path)
{
    const char *mime = client->response->mod->default_mime;
    size_t dot = 0;
    for(size_t i = 0; true; ++i)
    {
        const char c = path[i];
        if(!c)
            break;
        else if(c == '/')
            dot = 0;
        else if(c == '.')
            dot = i;
    }

    if(dot == 0)
        return mime;
    const char *extension = &path[dot + 1];

    lua_getglobal(lua, "mime");
    if(lua_istable(lua, -1))
    {
        lua_getfield(lua, -1, extension);
        if(lua_isstring(lua, -1))
            mime = lua_tostring(lua, -1);
        lua_pop(lua, 1); // extension
    }
    lua_pop(lua, 1); // mime
    return mime;
}

/* Stack: 1 - instance, 2 - server, 3 - client, 4 - request */
static int module_call(module_data_t *mod)
{
    http_client_t *client = (http_client_t *)lua_touserdata(lua, 3);

    if(lua_isnil(lua, 4))
    {
        if(client->response)
        {
            close(client->response->file_fd);
            free(client->response);
            client->response = NULL;
        }
        return 0;
    }

    client->response = (http_response_t *)calloc(1, sizeof(http_response_t));
    client->response->mod = mod;
    client->on_send = NULL;
    client->on_read = NULL;
    client->on_ready = on_ready_send_file;
    client->response->sock_fd = asc_socket_fd(client->sock);

    lua_rawgeti(lua, LUA_REGISTRYINDEX, client->idx_request);
    lua_getfield(lua, -1, __path);
    const char *path = lua_tostring(lua, -1);
    lua_pop(lua, 2); // request + path

    char *filename = (char *)malloc(PATH_MAX);
    sprintf(filename, "%s%s", mod->path, &path[mod->path_skip]);
    client->response->file_fd = open(filename, O_RDONLY);
    free(filename);
    if(client->response->file_fd == -1)
    {
        http_client_warning(client, "file not found %s", path);

        free(client->response);
        client->response = NULL;

        http_client_abort(client, 404, NULL);
        return 0;
    }

    struct stat sb;
    fstat(client->response->file_fd, &sb);

    if(!S_ISREG(sb.st_mode))
    {
        http_client_warning(client, "wrong file type %s", path);

        close(client->response->file_fd);
        free(client->response);
        client->response = NULL;

        http_client_abort(client, 404, NULL);
        return 0;
    }

    client->response->file_size = sb.st_size;

    http_response_code(client, 200, NULL);
    http_response_header(client, "Content-Length: %lu", client->response->file_size);
    http_response_header(client, "Content-Type: %s", lua_get_mime(client, path));
    http_response_send(client);

    return 0;
}

static int __module_call(lua_State *L)
{
    module_data_t *mod = (module_data_t *)lua_touserdata(L, lua_upvalueindex(1));
    return module_call(mod);
}

static void module_init(module_data_t *mod)
{
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
        mod->path_skip = luaL_len(lua, -1);
    lua_pop(lua, 1);

#ifdef ASC_SENDFILE
    int block_size = 0;
    module_option_number("block_size", &block_size);
    mod->block_size = (block_size > 0) ? (block_size * 1024) : ASC_SENDFILE;
#endif

    mod->default_mime = "application/octet-stream";
    module_option_string("default_mime", &mod->default_mime, NULL);

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
