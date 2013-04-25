/*
 * Astra UDP Module
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
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
 *      sync        - number, buffer size in TS-packets, by default 0 - syncing is disabled
 */

#include <astra.h>
#include <time.h>
#include <sys/time.h>

#define MSG(_msg) "[udp_output %s:%d] " _msg, mod->addr, mod->port

#define UDP_BUFFER_SIZE 1460
#define UDP_BUFFER_CAPACITY ((UDP_BUFFER_SIZE / TS_PACKET_SIZE) * TS_PACKET_SIZE)

#ifndef _WIN32
#   include <pthread.h>
#endif

struct module_data_t
{
    MODULE_STREAM_DATA();

    const char *addr;
    int port;
    int sync;

    int is_rtp;
    uint16_t rtpseq;

    asc_socket_t *sock;

    uint32_t buffer_skip;
    uint8_t buffer[UDP_BUFFER_SIZE];

#ifndef _WIN32
    uint64_t pcr;
    uint8_t *sync_buffer;
    uint32_t sync_buffer_size;
    uint32_t sync_buffer_count;
    uint32_t sync_buffer_read;
    uint32_t sync_buffer_write;
    uint32_t sync_buffer_overflow;

    asc_stream_t *stream;
    pthread_t thread;
    int thread_loop;
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
    if(mod->sync_buffer_count >= mod->sync_buffer_size)
    {
        ++mod->sync_buffer_overflow;
        return;
    }

    if(mod->sync_buffer_overflow)
    {
        asc_log_error(MSG("sync buffer overflow. dropped %d packets"), mod->sync_buffer_overflow);
        mod->sync_buffer_overflow = 0;
    }

    memcpy(&mod->sync_buffer[mod->sync_buffer_write], ts, TS_PACKET_SIZE);
    mod->sync_buffer_write += TS_PACKET_SIZE;
    if(mod->sync_buffer_write >= mod->sync_buffer_size)
        mod->sync_buffer_write = 0;

    mod->sync_buffer_count += TS_PACKET_SIZE;
}

static inline int check_pcr(const uint8_t *ts)
{
    return (   (ts[3] & 0x20)   /* adaptation field without payload */
            && (ts[4] > 0)      /* adaptation field length */
            && (ts[5] & 0x10)   /* PCR_flag */
            && !(ts[5] & 0x40)  /* skip random_access_indicator */
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
    for(count = TS_PACKET_SIZE; count < mod->sync_buffer_count; count += TS_PACKET_SIZE)
    {
        uint32_t pos = mod->sync_buffer_read + count;
        if(pos > mod->sync_buffer_size)
            pos -= mod->sync_buffer_size;

        if(check_pcr(&mod->sync_buffer[pos]))
        {
            *block_size = count;
            return 1;
        }
    }

    return 0;
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

static void * thread_loop(void *arg)
{
    module_data_t *mod = arg;

    // block sync
    struct timeval time_sync[2];
    struct timeval *time_sync_b = &time_sync[0]; // begin
    struct timeval *time_sync_e = &time_sync[1]; // end

    double block_time_total = 0;
    double block_accuracy, ts_accuracy;
    struct timespec ts_sync = { .tv_sec = 0, .tv_nsec = 0 };

    uint32_t pos;
    uint32_t block_size = 0;

    static const struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000 };

    while(mod->thread_loop)
    {
        asc_log_info(MSG("buffering..."));

        // flush
        mod->sync_buffer_count = 0;

        while(mod->thread_loop && mod->sync_buffer_count < (mod->sync_buffer_size / 2))
        {
            nanosleep(&ts, NULL);
            continue;
        }

        if(!seek_pcr(mod, &block_size))
        {
            asc_log_error(MSG("first PCR is not found"));
            continue;
        }
        pos = mod->sync_buffer_read + block_size;
        if(pos > mod->sync_buffer_size)
            pos -= mod->sync_buffer_size;
        mod->pcr = calc_pcr(&mod->sync_buffer[pos]);
        mod->sync_buffer_read = pos;

        block_accuracy = 0;
        ts_accuracy = 0;

        gettimeofday(time_sync_b, NULL);

        while(mod->thread_loop)
        {
            if(!seek_pcr(mod, &block_size))
            {
                asc_log_error(MSG("sync failed. Next PCR is not found. reload buffer"));
                break;
            }
            pos = mod->sync_buffer_read + block_size;
            if(pos > mod->sync_buffer_size)
                pos -= mod->sync_buffer_size;

            const double block_time = time_per_block(&mod->sync_buffer[pos], &mod->pcr);
            if(block_time < 0 || block_time > 200)
            {
                asc_log_warning(MSG("failed to get block time: %f"), block_time);
                mod->sync_buffer_read = pos;
                continue;
            }
            block_time_total += block_time;

            const long group_time = ((block_time + block_accuracy) * 1000000);

            if(block_size <= TS_PACKET_SIZE)
            {
                ts_sync.tv_nsec = group_time;
                nanosleep(&ts_sync, NULL);
                mod->sync_buffer_read = pos;
                continue;
            }

            ts_sync.tv_nsec = (group_time / (block_size / TS_PACKET_SIZE)) * ts_accuracy;

            while(block_size > 0)
            {
                on_ts(mod, &mod->sync_buffer[mod->sync_buffer_read]);
                mod->sync_buffer_read += TS_PACKET_SIZE;
                if(mod->sync_buffer_read >= mod->sync_buffer_size)
                    mod->sync_buffer_read = 0;
                mod->sync_buffer_count -= TS_PACKET_SIZE;
                block_size -= TS_PACKET_SIZE;
                nanosleep(&ts_sync, NULL);
            }

            gettimeofday(time_sync_e, NULL);
            const double time_sync_diff = timeval_diff(time_sync_b, time_sync_e);

            block_accuracy = block_time_total - time_sync_diff;
            if(block_accuracy > 0)
                ts_accuracy += 0.01;
            else
                ts_accuracy -= 0.01;
        }
    }

    pthread_exit(NULL);
}

#endif

static void module_init(module_data_t *mod)
{
    module_option_string("addr", &mod->addr);
    asc_assert(mod->addr != NULL, "[udp_output] option 'addr' is required");

    mod->port = 1234;
    module_option_number("port", &mod->port);

    module_option_number("rtp", &mod->is_rtp);
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

    mod->sock = asc_socket_open_udp4();
    asc_socket_set_reuseaddr(mod->sock, 1);
    if(!asc_socket_bind(mod->sock, NULL, 0))
        astra_abort();

    int value;
    if(module_option_number("socket_size", &value))
        asc_socket_set_buffer(mod->sock, 0, value);

    const char *localaddr = NULL;
    module_option_string("localaddr", &localaddr);
    if(localaddr)
        asc_socket_set_multicast_if(mod->sock, localaddr);

    value = 32;
    module_option_number("ttl", &value);
    asc_socket_set_multicast_ttl(mod->sock, value);

    asc_socket_multicast_join(mod->sock, mod->addr, NULL);
    asc_socket_set_sockaddr(mod->sock, mod->addr, mod->port);

#ifndef _WIN32
    if(module_option_number("sync", &mod->sync) && mod->sync)
    {
        module_stream_init(mod, sync_queue_push);

        mod->sync_buffer_size = TS_PACKET_SIZE * mod->sync;
        mod->sync_buffer = malloc(mod->sync_buffer_size);

        mod->thread_loop = 1;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
        const int ret = pthread_create(&mod->thread, &attr, thread_loop, mod);
        pthread_attr_destroy(&attr);
        asc_assert(ret == 0, MSG("failed to start thread"));
    }
    else
        module_stream_init(mod, on_ts);
#else
    module_stream_init(mod, on_ts);
#endif
}

static void module_destroy(module_data_t *mod)
{
    module_stream_destroy(mod);

#ifndef _WIN32
    if(mod->sync)
    {
        mod->thread_loop = 0;
        pthread_join(mod->thread, NULL);

        free(mod->sync_buffer);
    }
#endif

    asc_socket_close(mod->sock);
}

MODULE_STREAM_METHODS()
MODULE_LUA_METHODS()
{
    MODULE_STREAM_METHODS_REF()
};
MODULE_LUA_REGISTER(udp_output)
