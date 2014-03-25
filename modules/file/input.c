/*
 * Astra Module: File Input
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
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
#include <fcntl.h>

#define MSG(_msg) "[file_input %s] " _msg, mod->filename

#define INPUT_BUFFER_SIZE 2

struct module_data_t
{
    MODULE_LUA_DATA();
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

    int pause;
    bool reposition;
    bool is_eof;

    void *timer_skip;

    asc_thread_t *thread;
    asc_thread_buffer_t *thread_output;

    uint32_t overflow;
    uint8_t *buffer;
    uint32_t buffer_size;
    uint32_t buffer_skip;

    uint64_t pcr;
};

/* module code */

static inline int check_pcr(const uint8_t *ts)
{
    return (   (ts[3] & 0x20)   /* adaptation field without payload */
            && (ts[4] > 0)      /* adaptation field length */
            && (ts[5] & 0x10)   /* PCR_flag */
            && !(ts[5] & 0x40)  /* random_access_indicator */
            );
}

static inline uint64_t calc_pcr(const uint8_t *ts)
{
    const uint64_t pcr_base = (ts[6] << 25)
                            | (ts[7] << 17)
                            | (ts[8] << 9 )
                            | (ts[9] << 1 )
                            | (ts[10] >> 7);
    const uint64_t pcr_ext = ((ts[10] & 1) << 8) | (ts[11]);
    return (pcr_base * 300 + pcr_ext);
}

static bool seek_pcr(module_data_t *mod, uint32_t *block_size)
{
    uint32_t count = mod->buffer_skip + mod->m2ts_header + TS_PACKET_SIZE;

    while(count < mod->buffer_size)
    {
        if(check_pcr(&mod->buffer[mod->m2ts_header + count]))
        {
            *block_size = count - mod->buffer_skip;
            return true;
        }
        count += mod->m2ts_header + TS_PACKET_SIZE;
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

    mod->fd = open(mod->filename, O_RDONLY);
    if(mod->fd <= 0)
    {
        mod->fd = 0;
        return false;
    }

    struct stat sb;
    fstat(mod->fd, &sb);
    mod->file_size = sb.st_size;

    if(mod->file_size < mod->buffer_size)
    {
        asc_log_error(MSG("file size must be greater than buffer_size"));
        close(mod->fd);
        mod->fd = 0;
        return false;
    }

    if(mod->file_skip)
    {
        if(mod->file_skip >= mod->file_size)
        {
            asc_log_warning(MSG("skip value is greater than the file size"));
            mod->file_skip = 0;
        }
    }

    const ssize_t len = pread(mod->fd, mod->buffer, mod->buffer_size, mod->file_skip);
    if(mod->buffer_size != len)
    {
        asc_log_error(MSG("failed to read file"));
        close(mod->fd);
        mod->fd = 0;
        return false;
    }
    else if(mod->buffer[0] == 0x47 && mod->buffer[TS_PACKET_SIZE] == 0x47)
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

    uint32_t block_size = 0;
    if(!seek_pcr(mod, &block_size))
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
        const ssize_t r = pread(mod->fd, tail, M2TS_PACKET_SIZE
                                , mod->file_size - M2TS_PACKET_SIZE);
        if(r != M2TS_PACKET_SIZE || tail[4] != 0x47)
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
    mod->pcr = calc_pcr(&mod->buffer[mod->m2ts_header + mod->buffer_skip]);

    return true;
}

static void thread_loop(void *arg)
{
    module_data_t *mod = arg;

    // pause
    const struct timespec ts_pause = { .tv_sec = 0, .tv_nsec = 500000 };
    uint64_t pause_start, pause_stop;
    double pause_total;

    // block sync
    uint64_t time_sync_b, time_sync_e, time_sync_bb, time_sync_be;
    double block_time_total, total_sync_diff;
    uint32_t block_size = 0;

    if(!open_file(mod))
    {
        mod->is_eof = true;
        return;
    }

    struct timespec ts_sync = { .tv_sec = 0, .tv_nsec = 0 };

    bool reset_values = true;

    while(mod->fd > 0)
    {
        if(mod->pause)
        {
            while(mod->pause)
                nanosleep(&ts_pause, NULL);

            reset_values = true;
        }

        if(mod->reposition)
        {
            // reopen file
            if(!open_file(mod))
            {
                mod->is_eof = true;
                return;
            }

            mod->reposition = false;
            reset_values = true;
        }

        if(reset_values)
        {
            reset_values = false;
            time_sync_b = asc_utime();
            block_time_total = 0.0;
            total_sync_diff = 0.0;
            pause_total = 0.0;
        }

        if(!seek_pcr(mod, &block_size))
        {
            // try to load data
            mod->file_skip += mod->buffer_skip;
            const ssize_t len = pread(mod->fd, mod->buffer
                                      , mod->buffer_size
                                      , mod->file_skip);
            mod->buffer_skip = 0;

            if(len != (ssize_t)mod->buffer_size)
            {
                if(!mod->loop)
                {
                    mod->is_eof = true;
                    return;
                }

                mod->file_skip = 0;
                mod->reposition = true;
            }
            continue;
        }

        // get PCR
        const uint64_t pcr = calc_pcr(&mod->buffer[  mod->m2ts_header
                                                          + mod->buffer_skip
                                                          + block_size]);
        const uint64_t delta_pcr = pcr - mod->pcr;
        mod->pcr = pcr;
        // get block time
        const uint64_t dpcr_base = delta_pcr / 300;
        const uint64_t dpcr_ext = delta_pcr % 300;
        const double block_time = ((double)(dpcr_base / 90.0)     // 90 kHz
                                + (double)(dpcr_ext / 27000.0));  // 27 MHz
        if(block_time < 0 || block_time > 250)
        {
            asc_log_error(MSG("block time out of range: %.2f block_size:%u")
                          , block_time, block_size);
            mod->buffer_skip += block_size;

            reset_values = true;
            continue;
        }
        block_time_total += block_time;

        // calculate the sync time value
        if((block_time + total_sync_diff) > 0)
            ts_sync.tv_nsec = ((block_time + total_sync_diff) * 1000000)
                            / (block_size / (mod->m2ts_header + TS_PACKET_SIZE));
        else
            ts_sync.tv_nsec = 0;

        const uint64_t ts_sync_nsec = ts_sync.tv_nsec;

        uint64_t calc_block_time_ns = 0;
        time_sync_bb = asc_utime();

        double pause_block = 0.0;

        const size_t block_end = mod->buffer_skip + block_size;
        while(mod->fd > 0 && mod->buffer_skip < block_end)
        {
            if(mod->pause)
            {
                pause_start = asc_utime();
                while(mod->pause)
                    nanosleep(&ts_pause, NULL);
                pause_stop = asc_utime();
                if(pause_stop < pause_start)
                    mod->reposition = true; // timetravel
                else
                    pause_block += (pause_stop - pause_start) / 1000;
            }

            if(mod->reposition)
                break;

            // sending
            const uint8_t *ptr = &mod->buffer[mod->m2ts_header + mod->buffer_skip];
            if(ptr[0] == 0x47)
            {
                ssize_t r = asc_thread_buffer_write(mod->thread_output, ptr, TS_PACKET_SIZE);
                if(r != TS_PACKET_SIZE)
                {
                    ++mod->overflow;
                }
                else
                {
                    if(mod->overflow > 0)
                    {
                        asc_log_warning(MSG("sync buffer overflow. dropped %d packets")
                                        , mod->overflow);
                        mod->overflow = 0;
                    }
                }
            }

            mod->buffer_skip += mod->m2ts_header + TS_PACKET_SIZE;
            if(ts_sync.tv_nsec > 0)
                nanosleep(&ts_sync, NULL);

            // block syncing
            calc_block_time_ns += ts_sync_nsec;
            time_sync_be = asc_utime();
            if(time_sync_be < time_sync_bb)
                break; // timetravel
            const uint64_t real_block_time_ns = (time_sync_be - time_sync_bb) * 1000 - pause_block;
            ts_sync.tv_nsec = (real_block_time_ns > calc_block_time_ns) ? 0 : ts_sync_nsec;
        }
        pause_total += pause_block;

        if(mod->reposition)
            continue;

        // stream syncing
        time_sync_e = asc_utime();

        if(time_sync_e < time_sync_b)
        {
            // timetravel
            asc_log_warning(MSG("timetravel detected"));
            total_sync_diff = -1000000.0;
        }
        else
        {
            const double time_sync_diff = (time_sync_e - time_sync_b) / 1000.0;
            total_sync_diff = block_time_total - time_sync_diff - pause_total;
        }

        // reset buffer on changing the system time
        if(total_sync_diff < -100.0 || total_sync_diff > 100.0)
        {
            asc_log_warning(MSG("wrong syncing time: %.2fms. reset time values"), total_sync_diff);
            reset_values = true;
        }
    }
}

static void on_thread_close(void *arg)
{
    module_data_t *mod = arg;

    if(mod->fd > 0)
    {
        close(mod->fd);
        mod->fd = 0;
    }

    if(mod->thread)
    {
        asc_thread_close(mod->thread);
        mod->thread = NULL;
    }

    if(mod->thread_output)
    {
        asc_thread_buffer_destroy(mod->thread_output);
        mod->thread_output = NULL;
    }

    if(mod->is_eof && mod->idx_callback)
    {
        lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_callback);
        lua_call(lua, 0, 0);
    }
}

static void on_thread_read(void *arg)
{
    module_data_t *mod = arg;

    uint8_t ts[TS_PACKET_SIZE];
    ssize_t r = asc_thread_buffer_read(mod->thread_output, ts, TS_PACKET_SIZE);
    if(r != TS_PACKET_SIZE)
        return;

    module_stream_send(mod, ts);
}

static void timer_skip_set(void *arg)
{
    module_data_t *mod = arg;
    char skip_str[64];
    int fd = open(mod->lock, O_CREAT | O_WRONLY | O_TRUNC
                  , S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
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

static int method_pause(module_data_t *mod)
{
    mod->pause = lua_tonumber(lua, -1);
    return 0;
}

static int method_position(module_data_t *mod)
{
    // TODO: reposition
    asc_log_warning(MSG("the function is not implemented"));

    return 0;
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
    module_option_number("pause", &mod->pause);

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
    asc_thread_set_on_read(mod->thread, mod->thread_output, on_thread_read);
    asc_thread_set_on_close(mod->thread, on_thread_close);
    asc_thread_start(mod->thread, thread_loop);
}

static void module_destroy(module_data_t *mod)
{
    asc_timer_destroy(mod->timer_skip);

    if(mod->thread)
        on_thread_close(mod);

    if(mod->buffer)
        free(mod->buffer);

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
    { "length", method_length },
    { "pause", method_pause },
    { "position", method_position }
};

MODULE_LUA_REGISTER(file_input)
