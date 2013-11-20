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

#ifdef _WIN32
#   error not avail for win32
#endif

#include <sys/mman.h>
#include <fcntl.h>
#include <sys/socket.h>

#define MSG(_msg) "[file_input %s] " _msg, mod->filename

#if 0

struct module_data_t
{
    MODULE_LUA_DATA();
    MODULE_STREAM_DATA();

    int fd;
    asc_event_t *event;
    uint8_t buffer[TS_PACKET_SIZE * 7];
};

static void on_read(void *arg)
{
    module_data_t *mod = arg;

    size_t size = read(mod->fd, mod->buffer, sizeof(mod->buffer));
    if(size != sizeof(mod->buffer))
    {
        asc_event_close(mod->event);
        mod->event = NULL;
        close(mod->fd);
        mod->fd = 0;
        return;
    }

    for(size_t i = 0; i < size; i += TS_PACKET_SIZE)
        module_stream_send(mod, &mod->buffer[i]);
}

static void module_init(module_data_t *mod)
{
    const char *filename =  NULL;
    module_option_string("filename", &filename);

    module_stream_init(mod, NULL);

    mod->fd = open(filename, O_RDONLY);
    if(mod->fd <= 0)
        return;

    mod->event = asc_event_init(mod->fd, mod);
    asc_event_set_on_read(mod->event, on_read);
}

static void module_destroy(module_data_t *mod)
{
    asc_event_close(mod->event);
    if(mod->fd > 0)
        close(mod->fd);

    module_stream_destroy(mod);
}

MODULE_STREAM_METHODS()
MODULE_LUA_METHODS()
{
    MODULE_STREAM_METHODS_REF()
};

#else

#define SYNC_BUFFER_SIZE (TS_PACKET_SIZE * 2048)

struct module_data_t
{
    MODULE_LUA_DATA();
    MODULE_STREAM_DATA();

    const char *filename;
    const char *lock;
    int loop;

    int fd;
    size_t skip;
    int idx_callback;

    int ts_size; // 188 - TS, 192 - M2TS
    uint32_t start_time;
    uint32_t length;

    int pause;
    int reposition;

    void *timer_skip;

    // thread to module buffer
    struct
    {
        asc_thread_t *thread;

        int fd[2];
        asc_event_t *event;

        uint8_t *buffer;
        uint32_t buffer_size;
        uint32_t buffer_count;
        uint32_t buffer_read;
        uint32_t buffer_write;
        uint32_t buffer_overflow;
    } sync;
    uint64_t pcr;

    // input buffer
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

static inline int check_pcr(const uint8_t *ts)
{
    return (   (ts[3] & 0x20)   /* adaptation field without payload */
            && (ts[4] > 0)      /* adaptation field length */
            && (ts[5] & 0x10)   /* PCR_flag */
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

static double timeval_diff(struct timeval *start, struct timeval *end)
{
    const int64_t s_us = start->tv_sec * 1000000 + start->tv_usec;
    const int64_t e_us = end->tv_sec * 1000000 + end->tv_usec;
    return (double)(e_us - s_us) / 1000.0; // ms
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
    {
        if(mod->skip >= (size_t)sb.st_size)
        {
            asc_log_warning(MSG("skip value is greater than the file size"));
            mod->skip = 0;
        }
        else
            mod->buffer.ptr += mod->skip;
    }

    return reset_buffer(mod);
}

static void sync_queue_push(module_data_t *mod, const uint8_t *ts)
{
    if(mod->sync.buffer_count >= mod->sync.buffer_size)
    {
        ++mod->sync.buffer_overflow;
        return;
    }

    if(mod->sync.buffer_overflow)
    {
        asc_log_error(MSG("sync buffer overflow. dropped %d packets"), mod->sync.buffer_overflow);
        mod->sync.buffer_overflow = 0;
    }

    memcpy(&mod->sync.buffer[mod->sync.buffer_write], ts, TS_PACKET_SIZE);
    mod->sync.buffer_write += TS_PACKET_SIZE;
    if(mod->sync.buffer_write >= mod->sync.buffer_size)
        mod->sync.buffer_write = 0;

    __sync_fetch_and_add(&mod->sync.buffer_count, TS_PACKET_SIZE);

    uint8_t cmd[1] = { 0 };
    if(send(mod->sync.fd[0], cmd, sizeof(cmd), 0) != sizeof(cmd))
        asc_log_error(MSG("failed to push signal to queue\n"));
}

static void sync_queue_pop(module_data_t *mod, uint8_t *ts)
{
    uint8_t cmd[1];
    if(recv(mod->sync.fd[1], cmd, sizeof(cmd), 0) != sizeof(cmd))
        asc_log_error(MSG("failed to pop signal from queue\n"));

    memcpy(ts, &mod->sync.buffer[mod->sync.buffer_read], TS_PACKET_SIZE);
    mod->sync.buffer_read += TS_PACKET_SIZE;
    if(mod->sync.buffer_read >= mod->sync.buffer_size)
        mod->sync.buffer_read = 0;

    __sync_fetch_and_sub(&mod->sync.buffer_count, TS_PACKET_SIZE);
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
    double pause_total = 0.0;

    // block sync
    struct timeval time_sync[4];
    struct timeval *time_sync_b = &time_sync[0]; // begin
    struct timeval *time_sync_e = &time_sync[1]; // end
    struct timeval *time_sync_bb = &time_sync[2];
    struct timeval *time_sync_be = &time_sync[3];

    gettimeofday(time_sync_b, NULL);
    double block_time_total = 0.0;
    double total_sync_diff = 0.0;

    struct timespec ts_sync = { .tv_sec = 0, .tv_nsec = 0 };

    asc_thread_while(mod->sync.thread)
    {
        if(mod->pause)
        {
            while(mod->pause)
                nanosleep(&ts_pause, NULL);

            gettimeofday(time_sync_b, NULL);
            block_time_total = 0.0;
            total_sync_diff = 0.0;
            pause_total = 0.0;
        }

        if(mod->ts_size == TS_PACKET_SIZE)
            mod->buffer.block_end = seek_pcr_188(mod->buffer.ptr, mod->buffer.end);
        else
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

#define GET_TS_PTR(_ptr) ((mod->ts_size == TS_PACKET_SIZE) ? _ptr : &_ptr[4])

        // get PCR
        const uint64_t pcr = calc_pcr(GET_TS_PTR(mod->buffer.block_end));
        const uint64_t delta_pcr = pcr - mod->pcr;
        mod->pcr = pcr;
        // get block time
        const uint64_t dpcr_base = delta_pcr / 300;
        const uint64_t dpcr_ext = delta_pcr % 300;
        const double block_time = ((double)(dpcr_base / 90.0)     // 90 kHz
                                + (double)(dpcr_ext / 27000.0));  // 27 MHz
        if(block_time < 0 || block_time > 250)
        {
            asc_log_error(MSG("block time out of range: %.2f"), block_time);
            mod->buffer.ptr = mod->buffer.block_end;

            gettimeofday(time_sync_b, NULL);
            block_time_total = 0.0;
            total_sync_diff = 0.0;
            pause_total = 0.0;
            continue;
        }
        block_time_total += block_time;

        mod->skip += (mod->buffer.block_end - mod->buffer.ptr);
        const uint32_t block_size = (mod->buffer.block_end - mod->buffer.ptr) / mod->ts_size;
        // calculate the sync time value
        if((block_time + total_sync_diff) > 0)
            ts_sync.tv_nsec = ((block_time + total_sync_diff) * 1000000) / block_size;
        else
            ts_sync.tv_nsec = 0;
        // store the sync time value for later usage
        const long ts_sync_nsec = ts_sync.tv_nsec;
#if 0
        if(ts_sync_nsec > 1000000)
            printf("ts_sync_nsec: %ld\n", ts_sync_nsec);
#endif
        long calc_block_time_ns = 0;
        gettimeofday(time_sync_bb, NULL);

        double pause_block = 0.0;

        while(mod->buffer.ptr < mod->buffer.block_end)
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

            sync_queue_push(mod, GET_TS_PTR(mod->buffer.ptr));
            mod->buffer.ptr += mod->ts_size;
            if(ts_sync.tv_nsec > 0)
                nanosleep(&ts_sync, NULL);

            // block syncing
            calc_block_time_ns += ts_sync_nsec;
            gettimeofday(time_sync_be, NULL);
            const long real_block_time_ns
                = ((time_sync_be->tv_sec * 1000000 + time_sync_be->tv_usec)
                   - (time_sync_bb->tv_sec * 1000000 + time_sync_bb->tv_usec))
                * 1000
                - pause_block;

            ts_sync.tv_nsec = (real_block_time_ns > calc_block_time_ns) ? 0 : ts_sync_nsec;
        }
        pause_total += pause_block;

#undef GET_TS_PTR

        // stream syncing
        gettimeofday(time_sync_e, NULL);
        const double real_sync_diff = timeval_diff(time_sync_b, time_sync_e) - pause_total;
        total_sync_diff = block_time_total - real_sync_diff;
#if 0
        printf("syncing value: %.2f\n", total_sync_diff);
#endif
        // reset buffer on changing the system time
        if(total_sync_diff < -100.0 || total_sync_diff > 100.0)
        {
            asc_log_warning(MSG("wrong syncing time: %.2fms. reset time values"), total_sync_diff);
            gettimeofday(time_sync_b, NULL);
            block_time_total = 0.0;
            total_sync_diff = 0.0;
            pause_total = 0.0;
        }
    }

    close_file(mod);
}

static void on_thread_read(void *arg)
{
    module_data_t *mod = arg;

    uint8_t ts[TS_PACKET_SIZE];
    sync_queue_pop(mod, ts);
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

    int value;
    if(module_option_number("check_length", &value) && value)
    {
        open_file(mod);
        close_file(mod);
        return;
    }

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

    socketpair(AF_LOCAL, SOCK_STREAM, 0, mod->sync.fd);

    mod->sync.event = asc_event_init(mod->sync.fd[1], mod);
    asc_event_set_on_read(mod->sync.event, on_thread_read);
    mod->sync.buffer = malloc(SYNC_BUFFER_SIZE);
    mod->sync.buffer_size = SYNC_BUFFER_SIZE;

    asc_thread_init(&mod->sync.thread, thread_loop, mod);
}

static void module_destroy(module_data_t *mod)
{
    asc_timer_destroy(mod->timer_skip);
    asc_thread_destroy(&mod->sync.thread);

    asc_event_close(mod->sync.event);
    if(mod->sync.fd[0])
    {
        close(mod->sync.fd[0]);
        close(mod->sync.fd[1]);
    }

    if(mod->sync.buffer)
        free(mod->sync.buffer);

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

#endif

MODULE_LUA_REGISTER(file_input)
