/*
 * Astra Module: File Input
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2015, Andrey Dyldin <and@cesbo.com>
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
 *      file_input
 *
 * Module Options:
 *      filename    - string, input file name
 *      lock        - string, lock file name (to store reading position)
 *      loop        - boolean, if true play a file in an infinite loop
 *      callback    - function, call function on EOF, without parameters
 */

#include <astra.h>

#define MSG(_msg) "[file_input %s] " _msg, mod->filename

#define INPUT_BUFFER_SIZE 2

struct module_data_t
{
    MODULE_STREAM_DATA();

    const char *filename;
    const char *lock;
    bool loop;

    int fd;
    int idx_callback;
    size_t file_size;
    size_t file_skip; // file position

    uint8_t m2ts_header;
    uint32_t start_time;
    uint32_t length;

    bool is_eof;

    asc_timer_t *timer_skip;

    asc_thread_t *thread;
    asc_thread_buffer_t *thread_output;

    uint32_t overflow;
    uint8_t *buffer;
    uint32_t buffer_size;
    uint32_t buffer_skip;
    uint32_t buffer_end;

    uint64_t pcr;
    uint16_t pcr_pid;
};

/* module code */

static bool seek_pcr(module_data_t *mod, size_t *block_size, uint64_t *pcr)
{
    const uint8_t packet_size = mod->m2ts_header + TS_PACKET_SIZE;
    size_t count = mod->buffer_skip + packet_size;

    while(count < mod->buffer_end)
    {
        const uint8_t *ts = &mod->buffer[mod->m2ts_header + count];
        if(TS_IS_PCR(ts))
        {
            const uint16_t pid = TS_GET_PID(ts);
            if(mod->pcr_pid == 0)
                mod->pcr_pid = pid;

            if(mod->pcr_pid == pid)
            {
                *pcr = TS_GET_PCR(ts);
                *block_size = count - mod->buffer_skip;
                return true;
            }
        }
        count += packet_size;
    }

    return false;
}

static inline uint32_t m2ts_time(const uint8_t *ts)
{
    return (ts[0] << 24) | (ts[1] << 16) | (ts[2] << 8) | (ts[3]);
}

static bool open_file(module_data_t *mod)
{
    if(mod->fd)
        close(mod->fd);

    mod->fd = open(mod->filename, O_RDONLY | O_BINARY);
    if(mod->fd <= 0)
    {
        mod->fd = 0;
        return false;
    }

    struct stat sb;
    fstat(mod->fd, &sb);
    mod->file_size = sb.st_size;

    if(mod->file_skip)
    {
        if(mod->file_skip >= mod->file_size)
        {
            asc_log_warning(MSG("skip value is greater than the file size"));
            mod->file_skip = 0;
        }
    }

    const ssize_t len = pread(mod->fd, mod->buffer, mod->buffer_size, mod->file_skip);
    if(len < 0)
    {
        asc_log_error(MSG("failed to read file"));
        close(mod->fd);
        mod->fd = 0;
        return false;
    }
    mod->buffer_end = len;

    if(mod->buffer[0] == 0x47 && mod->buffer[TS_PACKET_SIZE] == 0x47)
        mod->m2ts_header = 0;
    else if(mod->buffer[4] == 0x47 && mod->buffer[4 + M2TS_PACKET_SIZE] == 0x47)
        mod->m2ts_header = 4;
    else
    {
        asc_log_error(MSG("wrong file format"));
        close(mod->fd);
        mod->fd = 0;
        return false;
    }

    size_t block_size = 0;
    if(!seek_pcr(mod, &block_size, &mod->pcr))
    {
        asc_log_error(MSG("first PCR is not found"));
        close(mod->fd);
        mod->fd = 0;
        return false;
    }

    if(mod->m2ts_header == 4)
    {
        mod->start_time = m2ts_time(mod->buffer) / 1000;

        uint8_t tail[M2TS_PACKET_SIZE];
        const ssize_t len = pread(  mod->fd, tail, M2TS_PACKET_SIZE
                                  , mod->file_size - M2TS_PACKET_SIZE);
        if(len != M2TS_PACKET_SIZE || tail[4] != 0x47)
        {
            asc_log_warning(MSG("failed to get M2TS file length"));
        }
        else
        {
            const uint32_t stop_time = m2ts_time(tail) / 1000;
            mod->length = stop_time - mod->start_time;
        }
    }

    mod->buffer_skip = block_size;

    return true;
}

static void thread_loop(void *arg)
{
    module_data_t *mod = (module_data_t *)arg;

    // block sync
    uint64_t   pcr
             , system_time, system_time_check
             , block_time, block_time_total = 0;
    size_t block_size = 0;

    bool reset = true;

    if(!open_file(mod))
    {
        mod->is_eof = true;
        return;
    }

    while(mod->fd > 0)
    {
        if(reset)
        {
            reset = false;
            block_time_total = asc_utime();
        }

        if(!seek_pcr(mod, &block_size, &pcr))
        {
            // try to load data
            mod->file_skip += mod->buffer_skip;
            const ssize_t len = pread(mod->fd, mod->buffer, mod->buffer_size, mod->file_skip);
            mod->buffer_end = (len > 0) ? len : 0;
            mod->buffer_skip = 0;
            if(!seek_pcr(mod, &block_size, &pcr))
            {
                if(!mod->loop)
                {
                    mod->is_eof = true;
                    return;
                }
                else
                {
                    mod->file_skip = 0;
                    if(!open_file(mod))
                    {
                        mod->is_eof = true;
                        return;
                    }
                    reset = true;
                    continue;
                }
            }
        }

        // get PCR
        block_time = mpegts_pcr_block_us(&mod->pcr, &pcr);
        if(block_time == 0 || block_time > 500000)
        {
            asc_log_error(  MSG("block time out of range: %llums block_size:%u")
                          , (uint64_t)(block_time / 1000), block_size);
            mod->buffer_skip += block_size;

            reset = true;
            continue;
        }

        system_time = asc_utime();
        if(block_time_total > system_time + 100)
            asc_usleep(block_time_total - system_time);

        const uint32_t ts_count = block_size / (mod->m2ts_header + TS_PACKET_SIZE);
        const uint32_t ts_sync = block_time / ts_count;
        const uint32_t block_time_tail = block_time % ts_count;

        system_time_check = asc_utime();

        const size_t block_end = mod->buffer_skip + block_size;
        while(mod->fd > 0 && mod->buffer_skip < block_end)
        {
            // send
            mod->buffer_skip += mod->m2ts_header;
            if(TS_PACKET_SIZE != asc_thread_buffer_write(  mod->thread_output
                                                         , &mod->buffer[mod->buffer_skip]
                                                         , TS_PACKET_SIZE))
            {
                ;
            }
            mod->buffer_skip += TS_PACKET_SIZE;

            system_time = asc_utime();
            block_time_total += ts_sync;

            if(  (system_time < system_time_check) /* <-0s */
               ||(system_time > system_time_check + 1000000)) /* >+1s */
            {
                asc_log_warning(MSG("system time changed"));
                mod->buffer_skip = block_end;
                reset = true;
                break;
            }
            system_time_check = system_time;

            if(block_time_total > system_time + 100)
                asc_usleep(block_time_total - system_time);
        }

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

static void on_thread_close(void *arg)
{
    module_data_t *mod = (module_data_t *)arg;

    if(mod->fd > 0)
    {
        close(mod->fd);
        mod->fd = 0;
    }

    ASC_FREE(mod->thread, asc_thread_destroy);
    ASC_FREE(mod->thread_output, asc_thread_buffer_destroy);

    if(mod->is_eof && mod->idx_callback)
    {
        lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_callback);
        lua_call(lua, 0, 0);
    }
}

static void on_thread_read(void *arg)
{
    module_data_t *mod = (module_data_t *)arg;

    uint8_t ts[TS_PACKET_SIZE];
    const ssize_t r = asc_thread_buffer_read(mod->thread_output, ts, TS_PACKET_SIZE);
    if(r != TS_PACKET_SIZE)
        return;

    module_stream_send(mod, ts);
}

static void timer_skip_set(void *arg)
{
    module_data_t *mod = (module_data_t *)arg;
    char skip_str[64];
    int fd = open(mod->lock, O_CREAT | O_WRONLY | O_TRUNC
#ifndef _WIN32
                  , S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
#else
                  , S_IRUSR | S_IWUSR);
#endif

    if(fd > 0)
    {
        const int l = sprintf(skip_str, "%lu", mod->file_skip);
        if(write(fd, skip_str, l) <= 0)
            {};
        close(fd);
    }
}

/* methods */

static int method_length(module_data_t *mod)
{
    lua_pushnumber(lua, mod->length);
    return 1;
}

/* required */

static void module_init(module_data_t *mod)
{
    module_option_string("filename", &mod->filename, NULL);

    int buffer_size = 0;
    if(!module_option_number("buffer_size", &buffer_size) || buffer_size <= 0)
        buffer_size = INPUT_BUFFER_SIZE;
    mod->buffer_size = buffer_size * 1024 * 1024;
    mod->buffer = malloc(mod->buffer_size);

    bool check_length;
    if(module_option_boolean("check_length", &check_length) && check_length)
    {
        open_file(mod);
        if(mod->fd > 0)
        {
            close(mod->fd);
            mod->fd = 0;
        }
        return;
    }

    module_option_string("lock", &mod->lock, NULL);
    module_option_boolean("loop", &mod->loop);

    // store callback in registry
    lua_getfield(lua, 2, "callback");
    if(lua_type(lua, -1) == LUA_TFUNCTION)
        mod->idx_callback = luaL_ref(lua, LUA_REGISTRYINDEX);
    else
        lua_pop(lua, 1);

    module_stream_init(mod, NULL);

    if(mod->lock)
    {
        int fd = open(mod->lock, O_RDONLY);
        if(fd)
        {
            char skip_str[64];
            const int l = read(fd, skip_str, sizeof(skip_str));
            if(l > 0)
                mod->file_skip = strtoul(skip_str, NULL, 10);
            close(fd);
        }
        mod->timer_skip = asc_timer_init(2000, timer_skip_set, mod);
    }

    mod->thread = asc_thread_init(mod);
    mod->thread_output = asc_thread_buffer_init(mod->buffer_size);
    asc_thread_start(  mod->thread
                     , thread_loop
                     , on_thread_read, mod->thread_output
                     , on_thread_close);
}

static void module_destroy(module_data_t *mod)
{
    asc_timer_destroy(mod->timer_skip);

    if(mod->thread)
        on_thread_close(mod);

    ASC_FREE(mod->buffer, free);

    if(mod->idx_callback)
    {
        luaL_unref(lua, LUA_REGISTRYINDEX, mod->idx_callback);
        mod->idx_callback = 0;
    }

    module_stream_destroy(mod);
}

MODULE_STREAM_METHODS()
MODULE_LUA_METHODS()
{
    MODULE_STREAM_METHODS_REF(),
    { "length", method_length }
};

MODULE_LUA_REGISTER(file_input)
