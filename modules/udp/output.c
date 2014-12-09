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
 *      cbr         - number, constant bitrate
 */

#include <astra.h>

#define MSG(_msg) "[udp_output %s:%d] " _msg, mod->addr, mod->port

#define UDP_BUFFER_SIZE 1460

struct module_data_t
{
    MODULE_STREAM_DATA();

    const char *addr;
    int port;
    uint32_t cbr;

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
    uint16_t pcr_pid;
};

static const uint8_t null_ts[TS_PACKET_SIZE] = { 0x47, 0x1F, 0xFF, 0x10, 0x00 };

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

    if(mod->packet.skip > UDP_BUFFER_SIZE - TS_PACKET_SIZE)
    {
        if(asc_socket_sendto(mod->sock, mod->packet.buffer, mod->packet.skip) == -1)
            asc_log_warning(MSG("error on send [%s]"), asc_socket_error());
        mod->packet.skip = 0;
    }
}

static void thread_input_push(module_data_t *mod, const uint8_t *ts)
{
    const ssize_t r = asc_thread_buffer_write(mod->thread_input, ts, TS_PACKET_SIZE);
    if(r != TS_PACKET_SIZE)
    {
        asc_log_debug(MSG("sync buffer overflow"));
        asc_thread_buffer_flush(mod->thread_input);
    }
}

static bool seek_pcr(  module_data_t *mod
                     , size_t *block_size, size_t *next_block
                     , uint64_t *pcr)
{
    size_t count;
    uint8_t *ptr;

    for(count = TS_PACKET_SIZE; count < mod->sync.buffer_count; count += TS_PACKET_SIZE)
    {
        size_t skip = mod->sync.buffer_read + count;
        if(skip >= mod->sync.buffer_size)
            skip -= mod->sync.buffer_size;

        ptr = &mod->sync.buffer[skip];

        if(TS_IS_PCR(ptr))
        {
            const uint16_t pid = TS_GET_PID(ptr);
            if(mod->pcr_pid == 0)
                mod->pcr_pid = pid;

            if(mod->pcr_pid == pid)
            {
                *block_size = count;
                *next_block = skip;
                *pcr = TS_GET_PCR(ptr);

                return true;
            }
        }
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
        // block sync
        uint64_t   pcr
                 , system_time, system_time_check
                 , block_time, block_time_total = 0;
        size_t block_size = 0, next_block;

        bool reset = true;

        asc_log_info(MSG("buffering..."));

        // flush
        asc_thread_buffer_flush(mod->thread_input);
        mod->sync.buffer_count = 0;
        mod->sync.buffer_write = 0;
        mod->sync.buffer_read = 0;

        while(   mod->is_thread_started
              && mod->sync.buffer_write < mod->sync.buffer_size)
        {
            const ssize_t r = asc_thread_buffer_read(  mod->thread_input
                                                     , &mod->sync.buffer[mod->sync.buffer_write]
                                                     ,   mod->sync.buffer_size
                                                       - mod->sync.buffer_write);
            if(r > 0)
                mod->sync.buffer_write += r;
            else
                asc_usleep(1000);
        }
        mod->sync.buffer_count = mod->sync.buffer_write;
        if(mod->sync.buffer_write == mod->sync.buffer_size)
            mod->sync.buffer_write = 0;

        if(!seek_pcr(mod, &block_size, &next_block, &mod->pcr))
        {
            asc_log_error(MSG("first PCR is not found"));
            continue;
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

                uint8_t *const pointer = &mod->sync.buffer[mod->sync.buffer_write];
                const ssize_t r = asc_thread_buffer_read(  mod->thread_input
                                                         , pointer
                                                         , tail);
                if(r > 0)
                {
                    mod->sync.buffer_write += r;
                    if(mod->sync.buffer_write >= mod->sync.buffer_size)
                        mod->sync.buffer_write = 0;
                    mod->sync.buffer_count += r;
                }
            }

            // get PCR
            if(!seek_pcr(mod, &block_size, &next_block, &pcr))
            {
                asc_log_error(MSG("next PCR is not found"));
                break;
            }
            block_time = mpegts_pcr_block_us(&mod->pcr, &pcr);
            if(block_time == 0 || block_time > 500000)
            {
                asc_log_debug(  MSG("block time out of range: %llums block_size:%u")
                              , (uint64_t)(block_time / 1000), block_size);

                mod->sync.buffer_count -= block_size;
                mod->sync.buffer_read = next_block;

                reset = true;
                continue;
            }

            system_time = asc_utime();
            if(block_time_total > system_time + 100)
                asc_usleep(block_time_total - system_time);

            uint32_t ts_count = block_size / TS_PACKET_SIZE;
            if(mod->cbr > 0)
            {
                uint32_t cbr_ts_count = mod->cbr * block_time / 1000000;
                if(cbr_ts_count > ts_count)
                    ts_count = cbr_ts_count;
            }
            uint32_t ts_sync = block_time / ts_count;
            uint32_t block_time_tail = block_time % ts_count;

            system_time_check = asc_utime();

            for(uint32_t i = 0; mod->is_thread_started && i < ts_count; ++i)
            {
                // sending
                if(mod->sync.buffer_read != next_block)
                {
                    const uint8_t *const pointer = &mod->sync.buffer[mod->sync.buffer_read];
                    on_ts(mod, pointer);

                    mod->sync.buffer_read += TS_PACKET_SIZE;
                    if(mod->sync.buffer_read >= mod->sync.buffer_size)
                        mod->sync.buffer_read = 0;
                }
                else
                {
                    on_ts(mod, null_ts);
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

    value = 0;
    module_option_number("sync", &value);
    if(value > 0)
    {
        module_stream_init(mod, thread_input_push);

        mod->sync.buffer_size = value * 1024 * 1024;
        mod->sync.buffer_size -= mod->sync.buffer_size % TS_PACKET_SIZE;
        mod->sync.buffer = malloc(mod->sync.buffer_size);

        value = 0;
        module_option_number("cbr", &value);
        if(value > 0)
            mod->cbr = (value * 1000 * 1000) / (8 * TS_PACKET_SIZE); // ts/s

        mod->thread = asc_thread_init(mod);
        mod->thread_input = asc_thread_buffer_init(mod->sync.buffer_size * 2);
        asc_thread_start(  mod->thread
                         , thread_loop
                         , NULL, NULL
                         , on_thread_close);
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
