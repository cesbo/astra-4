/*
 * Astra File Module
 * http://cesbo.com/
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
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

#ifdef _WIN32
#   error not avail for win32
#endif

#include <sys/mman.h>
#include <fcntl.h>

#define MSG(_msg) "[file_intput %s] " _msg, mod->filename

struct module_data_t
{
    MODULE_LUA_DATA();
    MODULE_STREAM_DATA();

    const char *filename;
    const char *lock;
    int loop;

    asc_thread_t *thread;
    asc_stream_t *stream;

    int fd;
    size_t skip;
    int idx_callback;

    int ts_size; // 188 - TS, 192 - M2TS
    uint32_t start_time;
    uint32_t length;

    int pause;
    int reposition;

    void *timer_skip;

    uint64_t pcr;

    struct
    {
        uint8_t *begin;
        uint8_t *ts_begin;
        uint8_t *ptr;
        uint8_t *end;
        uint8_t *block_end;
    } buffer;
};

/* module code */

static inline int check_pcr(uint8_t *ts)
{
    return (   (ts[3] & 0x20)   /* adaptation field without payload */
            && (ts[4] > 0)      /* adaptation field length */
            && (ts[5] & 0x10)   /* PCR_flag */
            && !(ts[5] & 0x40)  /* skip random_access_indicator */
            );
}

static inline uint64_t calc_pcr(uint8_t *ts)
{
    const uint64_t pcr_base = (ts[6] << 25)
                            | (ts[7] << 17)
                            | (ts[8] << 9 )
                            | (ts[9] << 1 )
                            | (ts[10] >> 7);
    const uint64_t pcr_ext = ((ts[10] & 1) << 8) | (ts[11]);
    return (pcr_base * 300 + pcr_ext);
}

static uint8_t * seek_pcr_188(uint8_t *buffer, uint8_t *buffer_end)
{
    buffer += TS_PACKET_SIZE;
    for(; buffer < buffer_end; buffer += TS_PACKET_SIZE)
    {
        if(check_pcr(buffer))
            return buffer;
    }

    return NULL;
}

static uint8_t * seek_pcr_192(uint8_t *buffer, uint8_t *buffer_end)
{
    buffer += M2TS_PACKET_SIZE;
    for(; buffer < buffer_end; buffer += M2TS_PACKET_SIZE)
    {
        if(check_pcr(&buffer[4]))
            return buffer;
    }

    return NULL;
}

static double time_per_block(uint8_t *block_end, uint64_t *last_pcr)
{
    uint64_t pcr = calc_pcr(block_end);

    const uint64_t dpcr = pcr - *last_pcr;
    *last_pcr = pcr;
    const uint64_t dpcr_base = dpcr / 300;
    const uint64_t dpcr_ext = dpcr % 300;

    const double dt = ((double)(dpcr_base / 90.0)     // 90 kHz
                    + (double)(dpcr_ext / 27000.0));  // 27 MHz

    return dt; // ms
}

static double timeval_diff(struct timeval *start, struct timeval *end)
{
    const int64_t s_us = start->tv_sec * 1000000 + start->tv_usec;
    const int64_t e_us = end->tv_sec * 1000000 + end->tv_usec;
    return (e_us - s_us) / 1000; // ms
}

static inline uint32_t m2ts_time(const uint8_t *ts)
{
    return (ts[0] << 24) | (ts[1] << 16) | (ts[2] << 8) | (ts[3]);
}

static void close_file(module_data_t *mod)
{
    if(!mod->fd)
        return;

    munmap(mod->buffer.begin, mod->buffer.end - mod->buffer.begin);
    close(mod->fd);
    mod->fd = 0;
}

static int reset_buffer(module_data_t *mod)
{
    // sync file position
    int i = 0;
    for(; i < 200; ++i)
    {
        if(mod->buffer.ptr[i] == 0x47)
        {
            if(mod->buffer.ptr[i + TS_PACKET_SIZE] == 0x47)
            {
                mod->buffer.ptr += i;
                mod->ts_size = TS_PACKET_SIZE;
                mod->buffer.ptr = seek_pcr_188(mod->buffer.ptr, mod->buffer.end);
                break;
            }
            else if(mod->buffer.ptr[i + M2TS_PACKET_SIZE] == 0x47)
            {
                mod->ts_size = M2TS_PACKET_SIZE;
                if(i >= 4) // go back to timestamp
                    mod->buffer.ptr += i - 4;
                else // go to next packet with timestamp
                    mod->buffer.ptr += TS_PACKET_SIZE;
                mod->buffer.ts_begin = mod->buffer.ptr;
                mod->buffer.ptr = seek_pcr_192(mod->buffer.ptr, mod->buffer.end);

                mod->start_time = m2ts_time(mod->buffer.ptr) / 1000;
                const size_t size = (((mod->buffer.end - mod->buffer.ptr)
                                     / M2TS_PACKET_SIZE) - 1) * M2TS_PACKET_SIZE;
                const uint32_t stop_time = m2ts_time(&mod->buffer.ptr[size]) / 1000;
                mod->length = stop_time - mod->start_time;

                break;
            }
        }
    }
    if(i == 200)
    {
        asc_log_error(MSG("failed to sync file"));
        close_file(mod);
        return 0;
    }

    if(!mod->buffer.ptr)
    {
        asc_log_error(MSG("first PCR is not found"));
        close_file(mod);
        return 0;
    }

    if(mod->ts_size == TS_PACKET_SIZE)
        mod->pcr = calc_pcr(mod->buffer.ptr);
    else
        mod->pcr = calc_pcr(&mod->buffer.ptr[4]);

    mod->buffer.block_end = NULL;
    return 1;
}

static int open_file(module_data_t *mod)
{
    if(mod->fd)
    {
        mod->skip = 0; // reopen file
        close(mod->fd);
    }

    mod->fd = open(mod->filename, O_RDONLY);
    if(mod->fd <= 0)
    {
        mod->fd = 0;
        return 0;
    }

    struct stat sb;
    fstat(mod->fd, &sb);
    mod->buffer.begin = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, mod->fd, 0);
    mod->buffer.ptr = mod->buffer.begin;
    mod->buffer.end = mod->buffer.begin + sb.st_size;

    if(mod->skip)
        mod->buffer.ptr += mod->skip;

    return reset_buffer(mod);
}

static void thread_loop(void *arg)
{
    module_data_t *mod = arg;

    if(!open_file(mod))
        return;

    // pause
    const struct timespec ts_pause = { .tv_sec = 0, .tv_nsec = 500000 };
    struct timeval pause_start;
    struct timeval pause_stop;
    double pause_total = 0;

    // block sync
    struct timeval time_sync[2];
    struct timeval *time_sync_b = &time_sync[0]; // begin
    struct timeval *time_sync_e = &time_sync[1]; // end
    gettimeofday(time_sync_b, NULL);
    double block_time_total = 0;

    double block_accuracy = 0;
    double ts_accuracy = 0.75;
    struct timespec ts_sync = { .tv_sec = 0, .tv_nsec = 0 };

    if(mod->ts_size == TS_PACKET_SIZE)
    {
        asc_thread_while(mod->thread)
        {
            mod->buffer.block_end = seek_pcr_188(mod->buffer.ptr, mod->buffer.end);
            if(!mod->buffer.block_end)
            {
                if(!mod->loop)
                {
                    if(mod->idx_callback)
                    {
                        lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_callback);
                        lua_call(lua, 0, 0);
                    }
                    break;
                }

                mod->buffer.ptr = mod->buffer.begin;
                reset_buffer(mod);
                continue;
            }

            const double block_time = time_per_block(mod->buffer.block_end, &mod->pcr);
            if(block_time < 0 || block_time > 200)
            {
                mod->buffer.ptr = mod->buffer.block_end;
                continue;
            }

            const uint32_t block_size = mod->buffer.block_end - mod->buffer.ptr;
            const uint32_t ts_count = block_size / TS_PACKET_SIZE;
            const long group_time = ((block_time + block_accuracy) * 1000000);
            ts_sync.tv_nsec = (group_time / ts_count) * ts_accuracy;

            uint8_t *const ptr_end = mod->buffer.block_end;
            while(mod->buffer.ptr < ptr_end)
            {
                asc_stream_send(mod->stream, mod->buffer.ptr, TS_PACKET_SIZE);
                mod->buffer.ptr += TS_PACKET_SIZE;
                nanosleep(&ts_sync, NULL);
            }

            gettimeofday(time_sync_e, NULL);
            const double time_sync_diff = timeval_diff(time_sync_b, time_sync_e);
            block_time_total += block_time;
            block_accuracy = block_time_total - time_sync_diff;
            if(block_accuracy > 0)
                ts_accuracy += 0.01;
            else
                ts_accuracy -= 0.01;
        }
    }
    else
    {
        asc_thread_while(mod->thread)
        {
            mod->buffer.block_end = seek_pcr_192(mod->buffer.ptr, mod->buffer.end);
            if(!mod->buffer.block_end)
            {
                if(!mod->loop)
                {
                    if(mod->idx_callback)
                    {
                        lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->idx_callback);
                        lua_call(lua, 0, 0);
                    }
                    break;
                }

                mod->buffer.ptr = mod->buffer.begin;
                reset_buffer(mod);
                continue;
            }

            const double block_time = time_per_block(&mod->buffer.block_end[4], &mod->pcr);
            if(block_time == -1)
            {
                mod->buffer.ptr = mod->buffer.block_end;
                continue;
            }

            const uint32_t block_size = mod->buffer.block_end - mod->buffer.ptr;
            const uint32_t ts_count = block_size / M2TS_PACKET_SIZE;
            const long group_time = ((block_time + block_accuracy) * 1000000);
            ts_sync.tv_nsec = (group_time / ts_count) * ts_accuracy;

            double pause_block = 0;
            uint8_t *const ptr_end = mod->buffer.block_end;
            while(mod->buffer.ptr < ptr_end)
            {
                if(mod->pause)
                {
                    gettimeofday(&pause_start, NULL);
                    while(mod->pause)
                        nanosleep(&ts_pause, NULL);
                    gettimeofday(&pause_stop, NULL);
                    pause_block += timeval_diff(&pause_start, &pause_stop);
                }
                if(mod->reposition)
                {
                    mod->reposition = 0;
                    break;
                }
                asc_stream_send(mod->stream, &mod->buffer.ptr[4], TS_PACKET_SIZE);
                mod->buffer.ptr += M2TS_PACKET_SIZE;
                nanosleep(&ts_sync, NULL);
            }
            pause_total += pause_block;

            gettimeofday(time_sync_e, NULL);
            const double time_sync_diff = timeval_diff(time_sync_b, time_sync_e) - pause_total;
            block_time_total += block_time;
            block_accuracy = block_time_total - time_sync_diff;
            if(block_accuracy > 0)
                ts_accuracy += 0.01;
            else
                ts_accuracy -= 0.01;
        }
    }

    close_file(mod);
}

static void thread_callback(void *arg)
{
    module_data_t *mod = arg;

    uint8_t ts[TS_PACKET_SIZE];
    ssize_t len = asc_stream_recv(mod->stream, ts, sizeof(ts));
    if(len == sizeof(ts))
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
        const int l = sprintf(skip_str, "%lu", mod->skip);
        if(write(fd, skip_str, l) <= 0)
            {};
        close(fd);
    }
}

/* methods */

static int method_length(module_data_t *mod)
{
    if(!mod->fd)
        lua_pushnumber(lua, 0);
    else
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
    if(lua_isnil(lua, -1))
    {
        lua_pushnumber(lua, 0); // TODO: push current time
        return 1;
    }

    uint32_t pos = lua_tonumber(lua, -1);
    if(pos >= mod->length || mod->ts_size != M2TS_PACKET_SIZE)
    {
        lua_pushnumber(lua, 0);
        return 1;
    }

    const uint32_t ts_count = (mod->buffer.end - mod->buffer.begin) / M2TS_PACKET_SIZE;
    const uint32_t ts_skip = (pos * ts_count) / mod->length;

    mod->buffer.ptr = mod->buffer.ts_begin + ts_skip * M2TS_PACKET_SIZE;

    mod->buffer.ptr = seek_pcr_192(mod->buffer.ptr, mod->buffer.end);
    mod->pcr = calc_pcr(&mod->buffer.ptr[4]);

    mod->reposition = 1;

    const uint32_t curr_time = m2ts_time(mod->buffer.ptr) / 1000;
    lua_pushnumber(lua, curr_time - mod->start_time);

    return 1;
}

/* required */

static void module_init(module_data_t *mod)
{
    module_option_string("filename", &mod->filename);
    module_option_string("lock", &mod->lock);
    module_option_number("loop", &mod->loop);
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
                mod->skip = strtoul(skip_str, NULL, 10);
            close(fd);
        }
        mod->timer_skip = asc_timer_init(2000, timer_skip_set, mod);
    }

    mod->stream = asc_stream_init(thread_callback, mod);
    asc_thread_init(&mod->thread, thread_loop, mod);
}

static void module_destroy(module_data_t *mod)
{
    asc_timer_destroy(mod->timer_skip);
    asc_thread_destroy(&mod->thread);
    asc_stream_destroy(mod->stream);

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
