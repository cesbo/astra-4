/*
 * Astra Module: UDP Output
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
 *      udp_output
 *
 * Module Options:
 *      upstream    - object, stream instance returned by module_instance:stream()
 *      addr        - string, source IP address
 *      port        - number, source UDP port
 *      ttl         - number, time to live
 *      localaddr   - string, IP address of the local interface
 *      socket_size - number, socket buffer size
 *      rtp         - boolean, use RTP instad RAW UDP
 *      sync        - number, if greater then 0, then use MPEG-TS syncing.
 *                            average value of the stream bitrate in megabit per second
 */

#include <astra.h>

#ifndef _WIN32
#   include <pthread.h>
#endif

#define MSG(_msg) "[udp_output %s:%d] " _msg, mod->addr, mod->port

#define UDP_BUFFER_SIZE 1460
#define UDP_BUFFER_CAPACITY ((UDP_BUFFER_SIZE / TS_PACKET_SIZE) * TS_PACKET_SIZE)

struct module_data_t
{
    MODULE_LUA_DATA();
    MODULE_STREAM_DATA();

    const char *addr;
    int port;

    bool is_rtp;
    uint16_t rtpseq;

    asc_socket_t *sock;

    uint32_t buffer_skip;
    uint8_t buffer[UDP_BUFFER_SIZE];

#ifndef _WIN32
    bool is_thread_started;
    asc_thread_t *thread;
    pthread_mutex_t mutex;

    struct
    {
        uint8_t *buffer;
        uint32_t buffer_size;
        uint32_t buffer_count;
        uint32_t buffer_read;
        uint32_t buffer_write;
        uint32_t buffer_overflow;

        bool reload;
    } sync;

    uint64_t pcr;
#endif
};

static void on_ts(module_data_t *mod, const uint8_t *ts)
{
    if(mod->is_rtp && mod->buffer_skip == 0)
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        const uint64_t msec = ((tv.tv_sec % 1000000) * 1000) + (tv.tv_usec / 1000);
        uint8_t *buffer = mod->buffer;

        buffer[2] = (mod->rtpseq >> 8) & 0xFF;
        buffer[3] = (mod->rtpseq     ) & 0xFF;

        buffer[4] = (msec >> 24) & 0xFF;
        buffer[5] = (msec >> 16) & 0xFF;
        buffer[6] = (msec >>  8) & 0xFF;
        buffer[7] = (msec      ) & 0xFF;

        ++mod->rtpseq;

        mod->buffer_skip += 12;
    }

    memcpy(&mod->buffer[mod->buffer_skip], ts, TS_PACKET_SIZE);
    mod->buffer_skip += TS_PACKET_SIZE;

    if(mod->buffer_skip >= UDP_BUFFER_CAPACITY)
    {
        if(asc_socket_sendto(mod->sock, &mod->buffer, mod->buffer_skip) == -1)
            asc_log_warning(MSG("error on send [%s]"), asc_socket_error());
        mod->buffer_skip = 0;
    }
}

#ifndef _WIN32

static void sync_queue_push(module_data_t *mod, const uint8_t *ts)
{
    if(mod->sync.reload)
    {
        mod->sync.buffer_count = 0;
        mod->sync.buffer_write = 0;
        mod->sync.reload = false;
    }

    pthread_mutex_lock(&mod->mutex);

    if(mod->sync.buffer_overflow || mod->sync.buffer_count >= mod->sync.buffer_size)
    {
        if(mod->sync.buffer_count > (mod->sync.buffer_size / 2))
        {
            ++mod->sync.buffer_overflow;
            pthread_mutex_unlock(&mod->mutex);
            return;
        }
        else
        {
            asc_log_error(MSG("sync buffer overflow. dropped %d packets")
                          , mod->sync.buffer_overflow);
            mod->sync.buffer_overflow = 0;
        }
    }

    memcpy(&mod->sync.buffer[mod->sync.buffer_write], ts, TS_PACKET_SIZE);
    mod->sync.buffer_write += TS_PACKET_SIZE;
    if(mod->sync.buffer_write >= mod->sync.buffer_size)
        mod->sync.buffer_write = 0;

    mod->sync.buffer_count += TS_PACKET_SIZE;
    pthread_mutex_unlock(&mod->mutex);
}

static void sync_queue_pop(module_data_t *mod)
{
    on_ts(mod, &mod->sync.buffer[mod->sync.buffer_read]);

    mod->sync.buffer_read += TS_PACKET_SIZE;
    if(mod->sync.buffer_read >= mod->sync.buffer_size)
        mod->sync.buffer_read = 0;

    pthread_mutex_lock(&mod->mutex);
    mod->sync.buffer_count -= TS_PACKET_SIZE;
    pthread_mutex_unlock(&mod->mutex);
}

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

static int seek_pcr(module_data_t *mod, uint32_t *block_size)
{
    uint32_t count;
    for(count = TS_PACKET_SIZE; count < mod->sync.buffer_count; count += TS_PACKET_SIZE)
    {
        uint32_t pos = mod->sync.buffer_read + count;
        if(pos >= mod->sync.buffer_size)
            pos -= mod->sync.buffer_size;

        if(check_pcr(&mod->sync.buffer[pos]))
        {
            *block_size = count;
            return 1;
        }
    }

    return 0;
}

static void on_thread_close(void *arg)
{
    module_data_t *mod = arg;

    mod->is_thread_started = false;

    if(mod->thread)
    {
        asc_thread_close(mod->thread);
        mod->thread = NULL;
    }
}

static void thread_loop(void *arg)
{
    module_data_t *mod = arg;

    // block sync
    uint64_t time_sync_b, time_sync_e, time_sync_bb, time_sync_be;

    double block_time_total, total_sync_diff;
    uint32_t pos;
    uint32_t block_size = 0;

    struct timespec ts_sync = { .tv_sec = 0, .tv_nsec = 0 };
    static const struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000 };

    while(mod->is_thread_started)
    {
        asc_log_info(MSG("buffering..."));

        // flush
        mod->sync.reload = true;
        mod->sync.buffer_count = 0;
        mod->sync.buffer_read = 0;

        while(mod->sync.buffer_count < (mod->sync.buffer_size / 2))
        {
            nanosleep(&ts, NULL);
            continue;
        }

        if(!seek_pcr(mod, &block_size))
        {
            asc_log_error(MSG("first PCR is not found"));
            continue;
        }
        pos = mod->sync.buffer_read + block_size;
        if(pos >= mod->sync.buffer_size)
            pos -= mod->sync.buffer_size;
        mod->pcr = calc_pcr(&mod->sync.buffer[pos]);
        mod->sync.buffer_read = pos;

        time_sync_b = asc_utime();
        block_time_total = 0.0;
        total_sync_diff = 0.0;

        while(mod->is_thread_started)
        {
            if(!seek_pcr(mod, &block_size))
            {
                asc_log_error(MSG("sync failed. Next PCR is not found. reload buffer"));
                break;
            }
            pos = mod->sync.buffer_read + block_size;
            if(pos >= mod->sync.buffer_size)
                pos -= mod->sync.buffer_size;

            // get PCR
            const uint64_t pcr = calc_pcr(&mod->sync.buffer[pos]);
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
                              , block_time, block_size / TS_PACKET_SIZE);
                mod->sync.buffer_read = pos;

                time_sync_b = asc_utime();
                block_time_total = 0.0;
                total_sync_diff = 0.0;
                continue;
            }
            block_time_total += block_time;

            // calculate the sync time value
            if((block_time + total_sync_diff) > 0)
                ts_sync.tv_nsec = ((block_time + total_sync_diff) * 1000000)
                                / (block_size / TS_PACKET_SIZE);
            else
                ts_sync.tv_nsec = 0;
            // store the sync time value for later usage
            const uint64_t ts_sync_nsec = ts_sync.tv_nsec;

            uint64_t calc_block_time_ns = 0;
            time_sync_bb = asc_utime();

            while(block_size > 0)
            {
                if(!mod->is_thread_started)
                    return;

                sync_queue_pop(mod);
                block_size -= TS_PACKET_SIZE;
                if(ts_sync.tv_nsec > 0)
                    nanosleep(&ts_sync, NULL);

                // block syncing
                calc_block_time_ns += ts_sync_nsec;
                time_sync_be = asc_utime();

                const uint64_t real_block_time_ns = (time_sync_be - time_sync_bb) * 1000;
                ts_sync.tv_nsec = (real_block_time_ns > calc_block_time_ns) ? 0 : ts_sync_nsec;
            }

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
                total_sync_diff = block_time_total - time_sync_diff;
            }

            // reset buffer on changing the system time
            if(total_sync_diff < -100.0 || total_sync_diff > 100.0)
            {
                asc_log_warning(MSG("wrong syncing time: %.2fms. reset time values")
                                , total_sync_diff);

                time_sync_b = asc_utime();
                block_time_total = 0.0;
                total_sync_diff = 0.0;
            }
        }
    }
}
#endif

static void module_init(module_data_t *mod)
{
    module_option_string("addr", &mod->addr, NULL);
    asc_assert(mod->addr != NULL, "[udp_output] option 'addr' is required");

    mod->port = 1234;
    module_option_number("port", &mod->port);

    module_option_boolean("rtp", &mod->is_rtp);
    if(mod->is_rtp)
    {
        const uint32_t rtpssrc = (uint32_t)rand();

#define RTP_PT_H261     31      /* RFC2032 */
#define RTP_PT_MP2T     33      /* RFC2250 */

        mod->buffer[0 ] = 0x80; // RTP version
        mod->buffer[1 ] = RTP_PT_MP2T;
        mod->buffer[8 ] = (rtpssrc >> 24) & 0xFF;
        mod->buffer[9 ] = (rtpssrc >> 16) & 0xFF;
        mod->buffer[10] = (rtpssrc >>  8) & 0xFF;
        mod->buffer[11] = (rtpssrc      ) & 0xFF;
    }

    mod->sock = asc_socket_open_udp4(mod);
    asc_socket_set_reuseaddr(mod->sock, 1);
    if(!asc_socket_bind(mod->sock, NULL, 0))
        astra_abort();

    int value;
    if(module_option_number("socket_size", &value))
        asc_socket_set_buffer(mod->sock, 0, value);

    const char *localaddr = NULL;
    module_option_string("localaddr", &localaddr, NULL);
    if(localaddr)
        asc_socket_set_multicast_if(mod->sock, localaddr);

    value = 32;
    module_option_number("ttl", &value);
    asc_socket_set_multicast_ttl(mod->sock, value);

    asc_socket_multicast_join(mod->sock, mod->addr, NULL);
    asc_socket_set_sockaddr(mod->sock, mod->addr, mod->port);

#ifndef _WIN32
    if(module_option_number("sync", &value) && value > 0)
    {
        module_stream_init(mod, sync_queue_push);

        // is a 1/5 of the storage for the one second of the stream
        value = (value * 1000 * 1000) / 8;
        value = (value / TS_PACKET_SIZE) * TS_PACKET_SIZE;

        mod->sync.buffer = malloc(value);
        mod->sync.buffer_size = value;

        pthread_mutex_init(&mod->mutex, NULL);
        mod->thread = asc_thread_init(mod);
        asc_thread_set_on_close(mod->thread, on_thread_close);
        asc_thread_start(mod->thread, thread_loop);
    }
    else
#endif
    {
        module_stream_init(mod, on_ts);
    }
}

static void module_destroy(module_data_t *mod)
{
    module_stream_destroy(mod);

#ifndef _WIN32
    if(mod->thread)
    {
        on_thread_close(mod);
        pthread_mutex_destroy(&mod->mutex);
    }

    if(mod->sync.buffer)
    {
        free(mod->sync.buffer);
        mod->sync.buffer = NULL;
    }
#endif

    if(mod->sock)
    {
        asc_socket_close(mod->sock);
        mod->sock = NULL;
    }
}

MODULE_STREAM_METHODS()
MODULE_LUA_METHODS()
{
    MODULE_STREAM_METHODS_REF()
};
MODULE_LUA_REGISTER(udp_output)
