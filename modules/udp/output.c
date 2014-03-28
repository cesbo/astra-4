/*
 * Astra Module: UDP Output
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

#define MSG(_msg) "[udp_output %s:%d] " _msg, mod->addr, mod->port

#define UDP_BUFFER_SIZE 1460
#define UDP_BUFFER_CAPACITY ((UDP_BUFFER_SIZE / TS_PACKET_SIZE) * TS_PACKET_SIZE)
#define SYNC_BUFFER_SIZE 188 * 4096

struct module_data_t
{
    MODULE_LUA_DATA();
    MODULE_STREAM_DATA();

    const char *addr;
    int port;

    bool is_rtp;
    uint16_t rtpseq;

    asc_socket_t *sock;

    struct
    {
        uint32_t skip;
        uint8_t buffer[UDP_BUFFER_SIZE];
    } packet;

    bool is_thread_started;
    asc_thread_t *thread;
    asc_thread_buffer_t *thread_input;
    uint32_t overflow;

    struct
    {
        uint8_t *buffer;
        uint32_t buffer_size;
        uint32_t buffer_count;
        uint32_t buffer_read;
        uint32_t buffer_write;

        bool reload;
    } sync;

    uint64_t pcr;
};

static void on_ts(module_data_t *mod, const uint8_t *ts)
{
    if(mod->is_rtp && mod->packet.skip == 0)
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        const uint64_t msec = ((tv.tv_sec % 1000000) * 1000) + (tv.tv_usec / 1000);

        mod->packet.buffer[2] = (mod->rtpseq >> 8) & 0xFF;
        mod->packet.buffer[3] = (mod->rtpseq     ) & 0xFF;

        mod->packet.buffer[4] = (msec >> 24) & 0xFF;
        mod->packet.buffer[5] = (msec >> 16) & 0xFF;
        mod->packet.buffer[6] = (msec >>  8) & 0xFF;
        mod->packet.buffer[7] = (msec      ) & 0xFF;

        ++mod->rtpseq;

        mod->packet.skip += 12;
    }

    memcpy(&mod->packet.buffer[mod->packet.skip], ts, TS_PACKET_SIZE);
    mod->packet.skip += TS_PACKET_SIZE;

    if(mod->packet.skip >= UDP_BUFFER_CAPACITY)
    {
        if(asc_socket_sendto(mod->sock, &mod->packet.buffer, mod->packet.skip) == -1)
            asc_log_warning(MSG("error on send [%s]"), asc_socket_error());
        mod->packet.skip = 0;
    }
}

static void thread_input_push(module_data_t *mod, const uint8_t *ts)
{
    const ssize_t r = asc_thread_buffer_write(mod->thread_input, ts, TS_PACKET_SIZE);
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

        if(mpegts_pcr_check(&mod->sync.buffer[pos]))
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

    if(mod->thread_input)
    {
        asc_thread_buffer_destroy(mod->thread_input);
        mod->thread_input = NULL;
    }
}

static void thread_loop(void *arg)
{
    module_data_t *mod = arg;

    mod->is_thread_started = true;

    while(mod->is_thread_started)
    {
        uint64_t time_sync_b, time_sync_bb;

        double block_time_total, total_sync_diff;
        uint32_t next_block, block_size = 0;

        struct timespec ts_sync = { .tv_sec = 0, .tv_nsec = 0 };
        static const struct timespec data_wait = { .tv_sec = 0, .tv_nsec = 100000 };

        asc_log_info(MSG("buffering..."));

        mod->sync.buffer_count = 0;
        mod->sync.buffer_write = 0;
        mod->sync.buffer_read = 0;

        while(   mod->is_thread_started
              && mod->sync.buffer_count < mod->sync.buffer_size)
        {
            const ssize_t r = asc_thread_buffer_read(mod->thread_input
                                                     , &mod->sync.buffer[mod->sync.buffer_count]
                                                     , TS_PACKET_SIZE);
            if(r == TS_PACKET_SIZE)
                mod->sync.buffer_count += TS_PACKET_SIZE;
            else
                nanosleep(&data_wait, NULL);
        }
        mod->sync.buffer_write = mod->sync.buffer_count;
        if(mod->sync.buffer_write == mod->sync.buffer_size)
            mod->sync.buffer_write = 0;

        if(!seek_pcr(mod, &block_size))
        {
            asc_log_error(MSG("first PCR is not found"));
            continue;
        }
        mod->sync.buffer_count -= block_size;
        next_block = mod->sync.buffer_read + block_size;
        if(next_block >= mod->sync.buffer_size)
            next_block -= mod->sync.buffer_size;
        mod->pcr = calc_pcr(&mod->sync.buffer[next_block]);
        mod->sync.buffer_read = next_block;

        time_sync_b = asc_utime();
        block_time_total = 0.0;
        total_sync_diff = 0.0;

        while(mod->is_thread_started)
        {
            while(   mod->is_thread_started
                  && mod->sync.buffer_count < mod->sync.buffer_size)
            {
                uint8_t *const pointer = &mod->sync.buffer[mod->sync.buffer_write];
                const ssize_t r = asc_thread_buffer_read(mod->thread_input
                                                         , pointer
                                                         , TS_PACKET_SIZE);
                if(r == TS_PACKET_SIZE)
                {
                    mod->sync.buffer_write += TS_PACKET_SIZE;
                    if(mod->sync.buffer_write >= mod->sync.buffer_size)
                        mod->sync.buffer_write = 0;
                    mod->sync.buffer_count += TS_PACKET_SIZE;
                }
                else
                    break;
            }

            if(!seek_pcr(mod, &block_size))
            {
                asc_log_error(MSG("next PCR is not found"));
                break;
            }

            next_block = mod->sync.buffer_read + block_size;
            if(next_block >= mod->sync.buffer_size)
                next_block -= mod->sync.buffer_size;
            mod->sync.buffer_count -= block_size;

            // get PCR
            const uint64_t pcr = calc_pcr(&mod->sync.buffer[next_block]);
            const double block_time = mpegts_pcr_block_ms(mod->pcr, pcr);
            mod->pcr = pcr;
            if(block_time < 0 || block_time > 250)
            {
                asc_log_error(MSG("block time out of range: %.2f block_size:%u")
                              , block_time, block_size / TS_PACKET_SIZE);
                mod->sync.buffer_read = next_block;

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

            while(mod->is_thread_started && mod->sync.buffer_read != next_block)
            {
                // sending
                const uint8_t *const pointer = &mod->sync.buffer[mod->sync.buffer_read];
                on_ts(mod, pointer);

                mod->sync.buffer_read += TS_PACKET_SIZE;
                if(mod->sync.buffer_read >= mod->sync.buffer_size)
                    mod->sync.buffer_read = 0;

                // sync block
                if(ts_sync.tv_nsec > 0)
                    nanosleep(&ts_sync, NULL);

                calc_block_time_ns += ts_sync_nsec;
                const uint64_t time_sync_be = asc_utime();
                if(time_sync_be < time_sync_bb)
                    break; // timetravel
                const uint64_t real_block_time_ns = (time_sync_be - time_sync_bb) * 1000;
                ts_sync.tv_nsec = (real_block_time_ns > calc_block_time_ns) ? 0 : ts_sync_nsec;
            }

            // stream syncing
            const uint64_t time_sync_e = asc_utime();

            if(time_sync_e < time_sync_b)
            {
                asc_log_warning(MSG("timetravel detected"));

                time_sync_b = asc_utime();
                block_time_total = 0.0;
                total_sync_diff = 0.0;
                continue;
            }

            const double time_sync_diff = (time_sync_e - time_sync_b) / 1000.0;
            total_sync_diff = block_time_total - time_sync_diff;

            // reset buffer on changing the system time
            if(total_sync_diff < -100.0 || total_sync_diff > 100.0)
            {
                asc_log_warning(MSG("wrong syncing time: %.2fms"), total_sync_diff);

                time_sync_b = asc_utime();
                block_time_total = 0.0;
                total_sync_diff = 0.0;
            }
        }
    }
}

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

        mod->packet.buffer[0 ] = 0x80; // RTP version
        mod->packet.buffer[1 ] = RTP_PT_MP2T;
        mod->packet.buffer[8 ] = (rtpssrc >> 24) & 0xFF;
        mod->packet.buffer[9 ] = (rtpssrc >> 16) & 0xFF;
        mod->packet.buffer[10] = (rtpssrc >>  8) & 0xFF;
        mod->packet.buffer[11] = (rtpssrc      ) & 0xFF;
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

    if(module_option_number("sync", &value) && value > 0)
    {
        module_stream_init(mod, thread_input_push);

        mod->sync.buffer_size = SYNC_BUFFER_SIZE;
        mod->sync.buffer = malloc(mod->sync.buffer_size);

        mod->thread = asc_thread_init(mod);
        mod->thread_input = asc_thread_buffer_init((value * 1000 * 1000) / 8);
        asc_thread_set_on_close(mod->thread, on_thread_close);
        asc_thread_start(mod->thread, thread_loop);
    }
    else
    {
        module_stream_init(mod, on_ts);
    }
}

static void module_destroy(module_data_t *mod)
{
    module_stream_destroy(mod);

    if(mod->thread)
        on_thread_close(mod);

    if(mod->sync.buffer)
    {
        free(mod->sync.buffer);
        mod->sync.buffer = NULL;
    }

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
