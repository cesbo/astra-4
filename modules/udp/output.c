/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#include <astra.h>

#include <time.h>
#include <sys/time.h>

#define UDP_BUFFER_SIZE 1316
#define TS_PACKET_SIZE 188

#define LOG_MSG(_msg) "[udp_output %s:%d] " _msg \
                      , mod->config.addr, mod->config.port

struct module_data_s
{
    MODULE_BASE();

    struct
    {
        char *addr;
        int port;
        int ttl;
        char *local;
        int socket_size;
        int rtp;
    } config;

    uint16_t rtpseq;

    int sock;
    void *sockaddr;

    size_t buffer_skip;
    uint8_t buffer[UDP_BUFFER_SIZE];
};

/* stream_ts callbacks */

static void callback_send_ts(module_data_t *mod, uint8_t *ts)
{
    if(mod->buffer_skip > UDP_BUFFER_SIZE - TS_PACKET_SIZE)
    {
        if(mod->buffer_skip == 0)
            return;
        if(socket_sendto(mod->sock, mod->buffer, mod->buffer_skip
                         , mod->sockaddr) == -1)
        {
            log_warning(LOG_MSG("error on send [%s]"), socket_error());
        }
        mod->buffer_skip = 0;
    }

    if(mod->buffer_skip == 0 && mod->config.rtp)
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

/* required */

static void module_init(module_data_t *mod)
{
    log_debug(LOG_MSG("init"));

    stream_ts_init(mod, callback_send_ts, NULL, NULL, NULL, NULL);

    if(mod->config.rtp)
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

    mod->sock = socket_open(SOCKET_PROTO_UDP, NULL, 0);
    if(mod->config.socket_size > 0)
        socket_set_buffer(mod->sock, 0, mod->config.socket_size);
    socket_multicast_set_if(mod->sock, mod->config.local);
    socket_multicast_set_ttl(mod->sock, mod->config.ttl);
    socket_multicast_join(mod->sock, mod->config.addr, NULL);
    mod->sockaddr = socket_sockaddr_init(mod->config.addr, mod->config.port);
}

static void module_destroy(module_data_t *mod)
{
    log_debug(LOG_MSG("destroy"));

    stream_ts_destroy(mod);

    socket_close(mod->sock);
    socket_sockaddr_destroy(mod->sockaddr);
}

MODULE_OPTIONS()
{
    OPTION_STRING("addr"       , config.addr       , 1, NULL)
    OPTION_NUMBER("port"       , config.port       , 0, 1234)
    OPTION_NUMBER("ttl"        , config.ttl        , 0, 0)
    OPTION_STRING("localaddr"  , config.local      , 0, NULL)
    OPTION_NUMBER("socket_size", config.socket_size, 0, 0)
    OPTION_NUMBER("rtp"        , config.rtp        , 0, 0)
};

MODULE_METHODS_EMPTY();

MODULE(udp_output)
