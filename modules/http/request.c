/*
 * Astra Module: HTTP Request
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
 *      http_request
 *
 * Module Options:
 *      host        - string, server hostname or IP address
 *      port        - number, server port (default: 80)
 *      path        - string, request path
 *      method      - string, method (default: "GET")
 *      version     - string, HTTP version (default: "HTTP/1.1")
 *      headers     - table, list of the request headers
 *      content     - string, request content
 *      stream      - boolean, true to read MPEG-TS stream
 *      sync        - boolean or number, enable stream synchronization
 *      timeout     - number, request timeout
 *      callback    - function,
 */

#include "http.h"

#define MSG(_msg)                                       \
    "[http_request %s:%d%s] " _msg, mod->config.host    \
                                  , mod->config.port    \
                                  , mod->config.path

struct module_data_t
{
    MODULE_STREAM_DATA();

    struct
    {
        const char *host;
        int port;
        const char *path;
        bool sync;
    } config;

    int timeout_ms;
    bool is_stream;

    int idx_self;

    asc_socket_t *sock;
    asc_timer_t *timeout;

    // request
    struct
    {
        int status; // 1 - connected, 2 - request done

        const char *buffer;
        size_t skip;
        size_t size;

        int idx_body;
    } request;

    bool is_head;
    bool is_connection_close;
    bool is_connection_keep_alive;

    // response
    char buffer[HTTP_BUFFER_SIZE];
    size_t buffer_skip;
    size_t chunk_left;

    int idx_response;
    int status_code;

    int status;         // 1 - empty line is found, 2 - request ready, 3 - release

    int idx_content;
    bool is_chunked;
    bool is_content_length;
    string_buffer_t *content;

    bool is_active;

    // stream
    bool is_thread_started;
    asc_thread_t *thread;
    asc_thread_buffer_t *thread_output;

    struct
    {
        uint8_t *buffer;
        size_t buffer_size;
        size_t buffer_count;
        size_t buffer_read;
        size_t buffer_write;
    } sync;

    uint64_t pcr;
};

static const char __path[] = "path";
static const char __method[] = "method";
static const char __version[] = "version";
static const char __headers[] = "headers";
static const char __content[] = "content";
static const char __callback[] = "callback";
static const char __stream[] = "stream";
static const char __code[] = "code";
static const char __message[] = "message";

static const char __default_method[] = "GET";
static const char __default_path[] = "/";
static const char __default_version[] = "HTTP/1.1";

static const char __connection[] = "Connection: ";
static const char __close[] = "close";
static const char __keep_alive[] = "keep-alive";

static void on_close(void *);

static void callback(module_data_t *mod)
{
    const int response = lua_gettop(lua);
    lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_self);
    lua_getfield(lua, -1, "__options");
    lua_getfield(lua, -1, "callback");
    lua_pushvalue(lua, -3);
    lua_pushvalue(lua, response);
    lua_call(lua, 2, 0);
    lua_pop(lua, 3); // self + options + response
}

static void call_error(module_data_t *mod, const char *msg)
{
    lua_newtable(lua);
    lua_pushnumber(lua, 0);
    lua_setfield(lua, -2, __code);
    lua_pushstring(lua, msg);
    lua_setfield(lua, -2, __message);
    callback(mod);
}

void timeout_callback(void *arg)
{
    module_data_t *mod = arg;

    asc_timer_destroy(mod->timeout);
    mod->timeout = NULL;

    if(mod->request.status == 0)
    {
        mod->status = -1;
        mod->request.status = -1;
        call_error(mod, "connection timeout");
    }
    else
    {
        mod->status = -1;
        mod->request.status = -1;
        call_error(mod, "response timeout");
    }

    on_close(mod);
}

static void on_thread_close(void *arg);

static void on_close(void *arg)
{
    module_data_t *mod = arg;

    if(mod->thread)
        on_thread_close(mod);

    if(!mod->sock)
        return;

    asc_socket_close(mod->sock);
    mod->sock = NULL;

    if(mod->timeout)
    {
        asc_timer_destroy(mod->timeout);
        mod->timeout = NULL;
    }

    if(mod->request.buffer)
    {
        if(mod->request.status == 1)
            free((void *)mod->request.buffer);
        mod->request.buffer = NULL;
    }

    if(mod->request.idx_body)
    {
        luaL_unref(lua, LUA_REGISTRYINDEX, mod->request.idx_body);
        mod->request.idx_body = 0;
    }

    if(mod->request.status == 0)
    {
        mod->request.status = -1;
        call_error(mod, "connection failed");
    }
    else if(mod->status == 0)
    {
        mod->request.status = -1;
        call_error(mod, "failed to parse response");
    }

    if(mod->status == 2)
    {
        mod->status = 3;

        lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_response);
        callback(mod);
    }

    if(mod->is_stream && mod->status == 3)
    {
        /* stream on_close */
        mod->status = -1;
        mod->request.status = -1;

        lua_pushnil(lua);
        callback(mod);
    }

    if(mod->sync.buffer)
    {
        free(mod->sync.buffer);
        mod->sync.buffer = NULL;
    }

    if(mod->idx_response)
    {
        luaL_unref(lua, LUA_REGISTRYINDEX, mod->idx_response);
        mod->idx_response = 0;
    }

    if(mod->idx_content)
    {
        luaL_unref(lua, LUA_REGISTRYINDEX, mod->idx_content);
        mod->idx_content = 0;
    }

    if(mod->idx_self)
    {
        luaL_unref(lua, LUA_REGISTRYINDEX, mod->idx_self);
        mod->idx_self = 0;
    }

    if(mod->content)
    {
        string_buffer_free(mod->content);
        mod->content = NULL;
    }

    lua_gc(lua, LUA_GCCOLLECT, 0);
}

/*
 *  oooooooo8 ooooooooooo oooooooooo  ooooooooooo      o      oooo     oooo
 * 888        88  888  88  888    888  888    88      888      8888o   888
 *  888oooooo     888      888oooo88   888ooo8       8  88     88 888o8 88
 *         888    888      888  88o    888    oo    8oooo88    88  888  88
 * o88oooo888    o888o    o888o  88o8 o888ooo8888 o88o  o888o o88o  8  o88o
 *
 */

static bool seek_pcr(  module_data_t *mod
                     , size_t *block_size, size_t *next_block
                     , uint64_t *pcr)
{
    size_t count;

    while(1)
    {
        if(mod->sync.buffer_count < 2 * TS_PACKET_SIZE)
            return false;

        count = mod->sync.buffer_read + TS_PACKET_SIZE;
        if(count >= mod->sync.buffer_size)
            count -= mod->sync.buffer_size;

        if(   mod->sync.buffer[mod->sync.buffer_read] == 0x47
           && mod->sync.buffer[count] == 0x47)
        {
            break;
        }

        ++mod->sync.buffer_read;
        if(mod->sync.buffer_read >= mod->sync.buffer_size)
            mod->sync.buffer_read = 0;

        --mod->sync.buffer_count;
    }

    uint8_t *ptr, ts[TS_PACKET_SIZE];

    size_t next_skip, skip = mod->sync.buffer_read + TS_PACKET_SIZE;
    if(skip >= mod->sync.buffer_size)
        skip -= mod->sync.buffer_size;

    for(  count = TS_PACKET_SIZE
        ; count < mod->sync.buffer_count
        ; count += TS_PACKET_SIZE)
    {
        ptr = &mod->sync.buffer[skip];

        next_skip = skip + TS_PACKET_SIZE;
        if(next_skip > mod->sync.buffer_size)
        {
            const size_t packet_head = mod->sync.buffer_size - skip;
            memcpy(ts, ptr, packet_head);
            next_skip -= mod->sync.buffer_size;
            memcpy(&ts[packet_head], mod->sync.buffer, next_skip);
            ptr = ts;
        }

        if(mpegts_pcr_check(ptr))
        {
            *block_size = count;
            *next_block = skip;
            *pcr = mpegts_pcr(ptr);

            return true;
        }

        skip = (next_skip == mod->sync.buffer_size) ? 0 : next_skip;
    }

    return false;
}

static void on_thread_close(void *arg)
{
    module_data_t *mod = arg;

    mod->is_thread_started = false;

    if(mod->thread)
    {
        asc_thread_destroy(mod->thread);
        mod->thread = NULL;
    }

    if(mod->thread_output)
    {
        asc_thread_buffer_destroy(mod->thread_output);
        mod->thread_output = NULL;
    }

    on_close(mod);
}

static void on_thread_read(void *arg)
{
    module_data_t *mod = arg;

    uint8_t ts[TS_PACKET_SIZE];
    const ssize_t r = asc_thread_buffer_read(mod->thread_output, ts, sizeof(ts));
    if(r == sizeof(ts))
        module_stream_send(mod, ts);
}

static void thread_loop(void *arg)
{
    module_data_t *mod = arg;

    uint8_t *ptr, ts[TS_PACKET_SIZE];

    mod->is_thread_started = true;

    while(mod->is_thread_started)
    {
        // block sync
        uint64_t   pcr
                 , system_time, system_time_check
                 , block_time, block_time_total = 0;
        size_t block_size = 0, next_block;

        bool reset = true;

        asc_log_info(MSG("buffering..."));

        // flush
        mod->sync.buffer_count = 0;
        mod->sync.buffer_write = 0;
        mod->sync.buffer_read = 0;

        // check timeout
        system_time_check = asc_utime();

        while(   mod->is_thread_started
              && mod->sync.buffer_write < mod->sync.buffer_size)
        {
            system_time = asc_utime();

            const ssize_t size = asc_socket_recv(  mod->sock
                                                 , &mod->sync.buffer[mod->sync.buffer_write]
                                                 , mod->sync.buffer_size - mod->sync.buffer_write);
            if(size > 0)
            {
                system_time_check = system_time;
                mod->sync.buffer_write += size;
            }
            else
            {
                if(system_time - system_time_check >= (uint32_t)mod->timeout_ms * 1000)
                {
                    asc_log_error(MSG("receiving timeout"));
                    return;
                }
                asc_usleep(1000);
            }
        }
        mod->sync.buffer_count = mod->sync.buffer_write;
        if(mod->sync.buffer_write == mod->sync.buffer_size)
            mod->sync.buffer_write = 0;

        if(!seek_pcr(mod, &block_size, &next_block, &mod->pcr))
        {
            asc_log_error(MSG("first PCR is not found"));
            return;
        }

        mod->sync.buffer_count -= block_size;
        mod->sync.buffer_read = next_block;

        reset = true;

        while(mod->is_thread_started)
        {
            if(reset)
            {
                reset = false;
                block_time_total = asc_utime();
            }

            if(   mod->is_thread_started
               && mod->sync.buffer_count < mod->sync.buffer_size)
            {
                const size_t tail = (mod->sync.buffer_read > mod->sync.buffer_write)
                                  ? (mod->sync.buffer_read - mod->sync.buffer_write)
                                  : (mod->sync.buffer_size - mod->sync.buffer_write);

                const ssize_t l = asc_socket_recv(  mod->sock
                                                  , &mod->sync.buffer[mod->sync.buffer_write]
                                                  , tail);
                if(l > 0)
                {
                    mod->sync.buffer_write += l;
                    if(mod->sync.buffer_write >= mod->sync.buffer_size)
                        mod->sync.buffer_write = 0;
                    mod->sync.buffer_count += l;
                }
            }

            // get PCR
            if(!seek_pcr(mod, &block_size, &next_block, &pcr))
            {
                asc_log_error(MSG("next PCR is not found"));
                break;
            }
            block_time = mpegts_pcr_block_us(&mod->pcr, &pcr);
            mod->pcr = pcr;
            if(block_time == 0 || block_time > 250000)
            {
                asc_log_error(  MSG("block time out of range: %llums block_size:%u")
                              , (uint64_t)(block_time / 1000), block_size);

                mod->sync.buffer_count -= block_size;
                mod->sync.buffer_read = next_block;

                reset = true;
                continue;
            }

            system_time = asc_utime();
            if(block_time_total > system_time + 100)
                asc_usleep(block_time_total - system_time);

            const uint32_t ts_count = block_size / TS_PACKET_SIZE;
            const uint32_t ts_sync = block_time / ts_count;
            const uint32_t block_time_tail = block_time % ts_count;

            system_time_check = asc_utime();

            while(mod->is_thread_started && mod->sync.buffer_read != next_block)
            {
                // sending
                ptr = &mod->sync.buffer[mod->sync.buffer_read];
                size_t next_packet = mod->sync.buffer_read + TS_PACKET_SIZE;
                if(next_packet < mod->sync.buffer_size)
                {
                    mod->sync.buffer_read = next_packet;
                }
                else if(next_packet > mod->sync.buffer_size)
                {
                    const size_t packet_head = mod->sync.buffer_size - mod->sync.buffer_read;
                    memcpy(ts, ptr, packet_head);
                    mod->sync.buffer_read = next_packet - mod->sync.buffer_size;
                    memcpy(&ts[packet_head], mod->sync.buffer, mod->sync.buffer_read);
                    ptr = ts;
                }
                else /* next_packet == mod->sync.buffer_size */
                {
                    mod->sync.buffer_read = 0;
                }

                const ssize_t write_size = asc_thread_buffer_write(  mod->thread_output
                                                                   , ptr
                                                                   , TS_PACKET_SIZE);
                if(write_size != TS_PACKET_SIZE)
                {
                    // overflow
                }

                system_time = asc_utime();
                block_time_total += ts_sync;

                if(  (system_time < system_time_check) /* <-0s */
                   ||(system_time > system_time_check + 1000000)) /* >+1s */
                {
                    asc_log_warning(MSG("system time changed"));

                    mod->sync.buffer_read = next_block;

                    reset = true;
                    break;
                }
                system_time_check = system_time;

                if(block_time_total > system_time + 100)
                    asc_usleep(block_time_total - system_time);
            }
            mod->sync.buffer_count -= block_size;

            if(reset)
                continue;

            system_time = asc_utime();
            if(system_time > block_time_total + 100000)
            {
                asc_log_warning(  MSG("wrong syncing time. -%llums")
                                , (system_time - block_time_total) / 1000);
                reset = true;
            }

            block_time_total += block_time_tail;
        }
    }
}

static void check_is_active(void *arg)
{
    module_data_t *mod = arg;

    if(mod->is_active)
    {
        mod->is_active = false;
        return;
    }

    asc_log_error(MSG("receiving timeout"));
    on_close(mod);
}

static void on_ts_read(void *arg)
{
    module_data_t *mod = arg;

    ssize_t size = asc_socket_recv(  mod->sock
                                   , &mod->sync.buffer[mod->sync.buffer_write]
                                   , mod->sync.buffer_size - mod->sync.buffer_write);
    if(size <= 0)
    {
        on_close(mod);
        return;
    }

    mod->is_active = true;
    mod->sync.buffer_write += size;
    mod->sync.buffer_read = 0;

    while(1)
    {
        while(mod->sync.buffer[mod->sync.buffer_read] != 0x47)
        {
            ++mod->sync.buffer_read;
            if(mod->sync.buffer_read >= mod->sync.buffer_write)
            {
                mod->sync.buffer_write = 0;
                return;
            }
        }

        const size_t next = mod->sync.buffer_read + TS_PACKET_SIZE;
        if(next > mod->sync.buffer_write)
        {
            const size_t tail = mod->sync.buffer_write - mod->sync.buffer_read;
            if(tail > 0)
                memmove(mod->sync.buffer, &mod->sync.buffer[mod->sync.buffer_read], tail);
            mod->sync.buffer_write = tail;
            return;
        }

        module_stream_send(mod, &mod->sync.buffer[mod->sync.buffer_read]);
        mod->sync.buffer_read += TS_PACKET_SIZE;
    }
}

/*
 * oooooooooo  ooooooooooo      o      ooooooooo
 *  888    888  888    88      888      888    88o
 *  888oooo88   888ooo8       8  88     888    888
 *  888  88o    888    oo    8oooo88    888    888
 * o888o  88o8 o888ooo8888 o88o  o888o o888ooo88
 *
 */

static void on_read(void *arg)
{
    module_data_t *mod = arg;

    if(mod->timeout)
    {
        asc_timer_destroy(mod->timeout);
        mod->timeout = NULL;
    }

    ssize_t size = asc_socket_recv(  mod->sock
                                   , &mod->buffer[mod->buffer_skip]
                                   , HTTP_BUFFER_SIZE - mod->buffer_skip);
    if(size <= 0)
    {
        on_close(mod);
        return;
    }

    if(mod->status == 3)
    {
        asc_log_warning(MSG("received data after response"));
        return;
    }

    size_t eoh = 0; // end of headers
    size_t skip = 0;
    mod->buffer_skip += size;

    if(mod->status == 0)
    {
        // check empty line
        while(skip < mod->buffer_skip)
        {
            if(mod->buffer[skip + 0] == '\n' && mod->buffer[skip + 1] == '\n')
            {
                eoh = skip + 2;
                mod->status = 1;
                break;
            }
            else if(   mod->buffer[skip + 0] == '\r' && mod->buffer[skip + 1] == '\n'
                    && mod->buffer[skip + 2] == '\r' && mod->buffer[skip + 3] == '\n')
            {
                eoh = skip + 4;
                mod->status = 1;
                break;
            }
            ++skip;
        }

        if(mod->status != 1)
            return;
    }

    if(mod->status == 1)
    {
        parse_match_t m[4];

        skip = 0;

/*
 *     oooooooooo  ooooooooooo  oooooooo8 oooooooooo
 *      888    888  888    88  888         888    888
 *      888oooo88   888ooo8     888oooooo  888oooo88
 * ooo  888  88o    888    oo          888 888
 * 888 o888o  88o8 o888ooo8888 o88oooo888 o888o
 *
 */

        if(!http_parse_response(mod->buffer, m))
        {
            call_error(mod, "failed to parse response line");
            on_close(mod);
            return;
        }

        lua_newtable(lua);
        const int response = lua_gettop(lua);

        lua_pushvalue(lua, -1);
        if(mod->idx_response)
            luaL_unref(lua, LUA_REGISTRYINDEX, mod->idx_response);
        mod->idx_response = luaL_ref(lua, LUA_REGISTRYINDEX);

        lua_pushlstring(lua, &mod->buffer[m[1].so], m[1].eo - m[1].so);
        lua_setfield(lua, response, __version);

        mod->status_code = atoi(&mod->buffer[m[2].so]);
        lua_pushnumber(lua, mod->status_code);
        lua_setfield(lua, response, __code);

        lua_pushlstring(lua, &mod->buffer[m[3].so], m[3].eo - m[3].so);
        lua_setfield(lua, response, __message);

        skip += m[0].eo;

/*
 *     ooooo ooooo ooooooooooo      o      ooooooooo  ooooooooooo oooooooooo   oooooooo8
 *      888   888   888    88      888      888    88o 888    88   888    888 888
 *      888ooo888   888ooo8       8  88     888    888 888ooo8     888oooo88   888oooooo
 * ooo  888   888   888    oo    8oooo88    888    888 888    oo   888  88o           888
 * 888 o888o o888o o888ooo8888 o88o  o888o o888ooo88  o888ooo8888 o888o  88o8 o88oooo888
 *
 */

        lua_newtable(lua);
        lua_pushvalue(lua, -1);
        lua_setfield(lua, response, __headers);
        const int headers = lua_gettop(lua);

        while(skip < eoh)
        {
            if(!http_parse_header(&mod->buffer[skip], m))
            {
                call_error(mod, "failed to parse response headers");
                on_close(mod);
                return;
            }

            if(m[1].eo == 0)
            { /* empty line */
                skip += m[0].eo;
                mod->status = 2;
                break;
            }

            lua_string_to_lower(&mod->buffer[skip], m[1].eo);
            lua_pushlstring(lua, &mod->buffer[skip + m[2].so], m[2].eo - m[2].so);
            lua_settable(lua, headers);

            skip += m[0].eo;
        }

        lua_getfield(lua, headers, "content-length");
        if(lua_isnumber(lua, -1))
        {
            mod->is_content_length = true;
            mod->chunk_left = lua_tonumber(lua, -1);
        }
        lua_pop(lua, 1); // content-length

        lua_getfield(lua, headers, "transfer-encoding");
        if(lua_isstring(lua, -1))
        {
            const char *encoding = lua_tostring(lua, -1);
            mod->is_chunked = (strcmp(encoding, "chunked") == 0);
        }
        lua_pop(lua, 1); // transfer-encoding

        if(mod->is_content_length || mod->is_chunked)
        {
            if(mod->content)
                free(mod->content);
            mod->content = string_buffer_alloc();
        }

        lua_pop(lua, 2); // headers + response

        if(   (mod->is_head)
           || (mod->status_code >= 100 && mod->status_code < 200)
           || (mod->status_code == 204)
           || (mod->status_code == 304))
        {
            lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_response);
            mod->status = 3;
            callback(mod);

            if(mod->is_connection_close)
                on_close(mod);

            return;
        }
    }

/*
 *       oooooooo8   ooooooo  oooo   oooo ooooooooooo ooooooooooo oooo   oooo ooooooooooo
 *     o888     88 o888   888o 8888o  88  88  888  88  888    88   8888o  88  88  888  88
 *     888         888     888 88 888o88      888      888ooo8     88 888o88      888
 * ooo 888o     oo 888o   o888 88   8888      888      888    oo   88   8888      888
 * 888  888oooo88    88ooo88  o88o    88     o888o    o888ooo8888 o88o    88     o888o
 *
 */

    if(mod->is_stream && mod->status_code == 200)
    {
        lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_response);
        lua_pushboolean(lua, mod->is_stream);
        lua_setfield(lua, -2, __stream);
        mod->status = 3;
        callback(mod);

        mod->sync.buffer = malloc(mod->sync.buffer_size);

        if(!mod->config.sync)
        {
            mod->timeout = asc_timer_init(mod->timeout_ms, check_is_active, mod);

            asc_socket_set_on_read(mod->sock, on_ts_read);
            asc_socket_set_on_ready(mod->sock, NULL);
            return;
        }

        asc_socket_set_on_read(mod->sock, NULL);
        asc_socket_set_on_ready(mod->sock, NULL);
        asc_socket_set_on_close(mod->sock, NULL);

        mod->thread = asc_thread_init(mod);
        mod->thread_output = asc_thread_buffer_init(mod->sync.buffer_size);
        asc_thread_start(  mod->thread
                         , thread_loop
                         , on_thread_read, mod->thread_output
                         , on_thread_close);

        return;
    }

    // Transfer-Encoding: chunked
    if(mod->is_chunked)
    {
        parse_match_t m[2];

        while(skip < mod->buffer_skip)
        {
            if(!mod->chunk_left)
            {
                if(!http_parse_chunk(&mod->buffer[skip], m))
                {
                    call_error(mod, "invalid chunk");
                    on_close(mod);
                    return;
                }

                mod->chunk_left = 0;
                for(size_t i = m[1].so; i < m[1].eo; ++i)
                {
                    char c = mod->buffer[skip + i];
                    if(c >= '0' && c <= '9')
                        mod->chunk_left = (mod->chunk_left << 4) | (c - '0');
                    else if(c >= 'a' && c <= 'f')
                        mod->chunk_left = (mod->chunk_left << 4) | (c - 'a' + 0x0A);
                    else if(c >= 'A' && c <= 'F')
                        mod->chunk_left = (mod->chunk_left << 4) | (c - 'A' + 0x0A);
                }
                skip += m[0].eo;

                if(!mod->chunk_left)
                {
                    lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_response);
                    string_buffer_push(lua, mod->content);
                    mod->content = NULL;
                    lua_setfield(lua, -2, __content);
                    mod->status = 3;
                    callback(mod);

                    if(mod->is_connection_close)
                    {
                        on_close(mod);
                        return;
                    }

                    break;
                }

                mod->chunk_left += 2;
            }

            const size_t tail = size - skip;
            if(mod->chunk_left <= tail)
            {
                string_buffer_addlstring(mod->content, &mod->buffer[skip], mod->chunk_left - 2);

                skip += mod->chunk_left;
                mod->chunk_left = 0;
            }
            else
            {
                string_buffer_addlstring(mod->content, &mod->buffer[skip], tail);
                mod->chunk_left -= tail;
                break;
            }
        }
    }

    // Content-Length: *
    if(mod->is_content_length)
    {
        const size_t tail = mod->buffer_skip - skip;

        if(mod->chunk_left > tail)
        {
            string_buffer_addlstring(  mod->content
                                     , &mod->buffer[skip]
                                     , mod->buffer_skip);
            mod->chunk_left -= tail;
        }
        else
        {
            string_buffer_addlstring(  mod->content
                                     , &mod->buffer[skip]
                                     , mod->chunk_left);
            mod->chunk_left = 0;

            lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_response);
            string_buffer_push(lua, mod->content);
            mod->content = NULL;
            lua_setfield(lua, -2, __content);
            mod->status = 3;
            callback(mod);

            if(mod->is_connection_close)
            {
                on_close(mod);
                return;
            }
        }
    }

    mod->buffer_skip = 0;
}

/*
 *  oooooooo8 ooooooooooo oooo   oooo ooooooooo
 * 888         888    88   8888o  88   888    88o
 *  888oooooo  888ooo8     88 888o88   888    888
 *         888 888    oo   88   8888   888    888
 * o88oooo888 o888ooo8888 o88o    88  o888ooo88
 *
 */

static void on_ready_send_content(void *arg)
{
    module_data_t *mod = arg;

    asc_assert(mod->request.size > 0, MSG("invalid content size"));

    const size_t rem = mod->request.size - mod->request.skip;
    const size_t cap = (rem > HTTP_BUFFER_SIZE) ? HTTP_BUFFER_SIZE : rem;

    const ssize_t send_size = asc_socket_send(  mod->sock
                                              , &mod->request.buffer[mod->request.skip]
                                              , cap);
    if(send_size == -1)
    {
        asc_log_error(MSG("failed to send content [%s]"), asc_socket_error());
        on_close(mod);
        return;
    }
    mod->request.skip += send_size;

    if(mod->request.skip >= mod->request.size)
    {
        mod->request.buffer = NULL;

        luaL_unref(lua, LUA_REGISTRYINDEX, mod->request.idx_body);
        mod->request.idx_body = 0;

        mod->request.status = 3;

        asc_socket_set_on_ready(mod->sock, NULL);
    }
}

static void on_ready_send_request(void *arg)
{
    module_data_t *mod = arg;

    asc_assert(mod->request.size > 0, MSG("invalid request size"));

    const size_t rem = mod->request.size - mod->request.skip;
    const size_t cap = (rem > HTTP_BUFFER_SIZE) ? HTTP_BUFFER_SIZE : rem;

    const ssize_t send_size = asc_socket_send(  mod->sock
                                              , &mod->request.buffer[mod->request.skip]
                                              , cap);
    if(send_size == -1)
    {
        asc_log_error(MSG("failed to send response [%s]"), asc_socket_error());
        on_close(mod);
        return;
    }
    mod->request.skip += send_size;

    if(mod->request.skip >= mod->request.size)
    {
        free((void *)mod->request.buffer);
        mod->request.buffer = NULL;

        if(mod->request.idx_body)
        {
            lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->request.idx_body);
            mod->request.buffer = lua_tostring(lua, -1);
            mod->request.size = luaL_len(lua, -1);
            mod->request.skip = 0;
            lua_pop(lua, 1);

            mod->request.status = 2;

            asc_socket_set_on_ready(mod->sock, on_ready_send_content);
        }
        else
        {
            mod->request.status = 3;

            asc_socket_set_on_ready(mod->sock, NULL);
        }
    }
}

static void lua_make_request(module_data_t *mod)
{
    asc_assert(lua_istable(lua, -1), MSG("%s() required table on top of the stack"));

    lua_getfield(lua, -1, __method);
    const char *method = lua_isstring(lua, -1) ? lua_tostring(lua, -1) : __default_method;
    lua_pop(lua, 1);

    mod->is_head = (strcmp(method, "HEAD") == 0);

    lua_getfield(lua, -1, __path);
    mod->config.path = lua_isstring(lua, -1) ? lua_tostring(lua, -1) : __default_path;
    lua_pop(lua, 1);

    lua_getfield(lua, -1, __version);
    const char *version = lua_isstring(lua, -1) ? lua_tostring(lua, -1) : __default_version;
    lua_pop(lua, 1);

    string_buffer_t *buffer = string_buffer_alloc();

    string_buffer_addfstring(buffer, "%s %s %s\r\n", method, mod->config.path, version);

    lua_getfield(lua, -1, __headers);
    if(lua_istable(lua, -1))
    {
        for(lua_pushnil(lua); lua_next(lua, -2); lua_pop(lua, 1))
        {
            const char *h = lua_tostring(lua, -1);

            if(!strncasecmp(h, __connection, sizeof(__connection) - 1))
            {
                const char *hp = &h[sizeof(__connection) - 1];
                if(!strncasecmp(hp, __close, sizeof(__close) - 1))
                    mod->is_connection_close = true;
                else if(!strncasecmp(hp, __keep_alive, sizeof(__keep_alive) - 1))
                    mod->is_connection_keep_alive = true;
            }

            string_buffer_addfstring(buffer, "%s\r\n", h);
        }
    }
    lua_pop(lua, 1); // headers

    string_buffer_addlstring(buffer, "\r\n", 2);

    mod->request.buffer = string_buffer_release(buffer, &mod->request.size);
    mod->request.skip = 0;

    if(mod->request.idx_body)
    {
        luaL_unref(lua, LUA_REGISTRYINDEX, mod->request.idx_body);
        mod->request.idx_body = 0;
    }

    lua_getfield(lua, -1, __content);
    if(lua_isstring(lua, -1))
        mod->request.idx_body = luaL_ref(lua, LUA_REGISTRYINDEX);
    else
        lua_pop(lua, 1);
}

static void on_connect(void *arg)
{
    module_data_t *mod = arg;

    mod->request.status = 1;

    asc_timer_destroy(mod->timeout);
    mod->timeout = asc_timer_init(mod->timeout_ms, timeout_callback, mod);

    lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_self);
    lua_getfield(lua, -1, "__options");
    lua_make_request(mod);
    lua_pop(lua, 2); // self + __options

    asc_socket_set_on_read(mod->sock, on_read);
    asc_socket_set_on_ready(mod->sock, on_ready_send_request);
}

/*
 * oooo     oooo  ooooooo  ooooooooo  ooooo  oooo ooooo       ooooooooooo
 *  8888o   888 o888   888o 888    88o 888    88   888         888    88
 *  88 888o8 88 888     888 888    888 888    88   888         888ooo8
 *  88  888  88 888o   o888 888    888 888    88   888      o  888    oo
 * o88o  8  o88o  88ooo88  o888ooo88    888oo88   o888ooooo88 o888ooo8888
 *
 */

static int method_close(module_data_t *mod)
{
    mod->status = -1;
    mod->request.status = -1;
    on_close(mod);
    return 0;
}

static void module_init(module_data_t *mod)
{
    module_option_string("host", &mod->config.host, NULL);
    asc_assert(mod->config.host != NULL, MSG("option 'host' is required"));

    mod->config.port = 80;
    module_option_number("port", &mod->config.port);

    mod->config.path = __default_path;
    module_option_string("path", &mod->config.path, NULL);

    lua_getfield(lua, 2, __callback);
    asc_assert(lua_isfunction(lua, -1), MSG("option 'callback' is required"));
    lua_pop(lua, 1); // callback

    // store self in registry
    lua_pushvalue(lua, 3);
    mod->idx_self = luaL_ref(lua, LUA_REGISTRYINDEX);

    module_option_boolean(__stream, &mod->is_stream);
    if(mod->is_stream)
    {
        module_stream_init(mod, NULL);

        int value = 0;
        module_option_number("sync", &value);
        if(value > 0)
            mod->config.sync = true;
        else
            value = 1;

        mod->sync.buffer_size = value * 1024 * 1024;
    }

    mod->timeout_ms = 10;
    module_option_number("timeout", &mod->timeout_ms);
    mod->timeout_ms *= 1000;
    mod->timeout = asc_timer_init(mod->timeout_ms, timeout_callback, mod);

    mod->sock = asc_socket_open_tcp4(mod);
    asc_socket_connect(mod->sock, mod->config.host, mod->config.port, on_connect, on_close);
}

static void module_destroy(module_data_t *mod)
{
    if(mod->is_stream)
        module_stream_destroy(mod);

    mod->status = -1;
    mod->request.status = -1;

    on_close(mod);
}

MODULE_STREAM_METHODS()
MODULE_LUA_METHODS()
{
    MODULE_STREAM_METHODS_REF(),
    { "close", method_close }
};

MODULE_LUA_REGISTER(http_request)
