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
 */

#include <astra.h>

#include <time.h>
#include <sys/time.h>

#define UDP_BUFFER_SIZE 1460
#define TS_PACKET_SIZE 188

struct module_data_t
{
    MODULE_STREAM_DATA();

    char *addr;
    int port;

    int is_rtp;
    uint16_t rtpseq;

    socket_t *sock;

    size_t buffer_skip;
    uint8_t buffer[UDP_BUFFER_SIZE];
};

static void on_ts(module_data_t *mod, const uint8_t *ts)
{
    if(mod->buffer_skip > UDP_BUFFER_SIZE - TS_PACKET_SIZE)
    {
        if(mod->buffer_skip == 0)
            return;
        if(socket_sendto(mod->sock, mod->buffer, mod->buffer_skip) == -1)
            log_warning("[udp_output %s:%d] error on send [%s]", "", 0, socket_error());

        mod->buffer_skip = 0;
    }

    if(mod->buffer_skip == 0 && mod->is_rtp)
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        const uint64_t msec
            = ((tv.tv_sec % 1000000) * 1000) + (tv.tv_usec / 1000);
        uint8_t *buffer = mod->buffer;

        buffer[2] = (mod->rtpseq >> 8) & 0xFF;
        buffer[3] = (mod->rtpseq     ) & 0xFF;

        buffer[4] = (msec >> 24) & 0xFF;
        buffer[5] = (msec >> 16) & 0xFF;
        buffer[6] = (msec >>  8) & 0xFF;
        buffer[7] = (msec      ) & 0xFF;

        mod->buffer_skip = 12; // 12 - RTP header size
        ++mod->rtpseq;
    }

    memcpy(&mod->buffer[mod->buffer_skip], ts, TS_PACKET_SIZE);
    mod->buffer_skip += TS_PACKET_SIZE;
}

static void module_init(module_data_t *mod)
{
    module_stream_init(mod, on_ts);

    const char *addr = NULL;
    const int addr_len = module_option_string("addr", &addr);
    if(!addr)
    {
        log_error("[udp_output] option 'addr' is required");
        astra_abort();
    }
    mod->addr = malloc(addr_len + 1);
    strcpy(mod->addr, addr);

    mod->port = 1234;
    module_option_number("port", &mod->port);

    module_option_number("rtp", &mod->is_rtp);
    if(mod->is_rtp)
    {
        srand((uint32_t)time(NULL));
        const uint32_t rtpssrc = (uint32_t)rand();
        uint8_t *buffer = mod->buffer;

#define RTP_PT_H261     31      /* RFC2032 */
#define RTP_PT_MP2T     33      /* RFC2250 */

        buffer[0] = 0x80; // RTP version
        buffer[1] = RTP_PT_MP2T;

        buffer[8 ] = (rtpssrc >> 24) & 0xFF;
        buffer[9 ] = (rtpssrc >> 16) & 0xFF;
        buffer[10] = (rtpssrc >>  8) & 0xFF;
        buffer[11] = (rtpssrc      ) & 0xFF;
    }

    mod->sock = socket_open_udp4();
    socket_set_reuseaddr(mod->sock, 1);
    socket_bind(mod->sock, NULL, 0);

    int value;
    if(module_option_number("socket_size", &value))
        socket_set_buffer(mod->sock, 0, value);

    const char *localaddr = NULL;
    module_option_string("localaddr", &localaddr);
    if(localaddr)
        socket_set_multicast_if(mod->sock, localaddr);

    value = 32;
    module_option_number("ttl", &value);
    socket_set_multicast_ttl(mod->sock, value);

    socket_multicast_join(mod->sock, mod->addr, NULL);
    socket_set_sockaddr(mod->sock, mod->addr, mod->port);
}

static void module_destroy(module_data_t *mod)
{
    module_stream_destroy(mod);

    socket_close(mod->sock);
}

MODULE_STREAM_METHODS()
MODULE_LUA_METHODS()
{
    MODULE_STREAM_METHODS_REF()
};
MODULE_LUA_REGISTER(udp_output)
