/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#include <astra.h>

#define UDP_BUFFER_SIZE 1316
#define TS_PACKET_SIZE 188

#define LOG_MSG(_msg) "[udp_input %s:%d] " _msg \
                      , mod->config.addr, mod->config.port

struct module_data_s
{
    MODULE_BASE();

    struct
    {
        char *addr;
        int port;
        char *local;
        int socket_size;
        int rtp;
    } config;

    int sock;

    uint8_t buffer[UDP_BUFFER_SIZE];
};

void udp_input_callback(void *arg, int event)
{
    module_data_t *mod = (module_data_t *)arg;

    if(event == EVENT_ERROR)
    {
        event_detach(mod->sock);
        socket_close(mod->sock);
        mod->sock = 0;
        return;
    }

    ssize_t len = socket_recv(mod->sock, mod->buffer, UDP_BUFFER_SIZE);
    if(len <= 0)
    {
        udp_input_callback(arg, EVENT_ERROR);
        return;
    }

    // 12 - RTP header size
    ssize_t i = (mod->config.rtp) ? 12 : 0;
    for(; i < len; i += TS_PACKET_SIZE)
        stream_ts_send(mod, &mod->buffer[i]);
}

/* methods */

static int method_attach(module_data_t *mod)
{
    stream_ts_attach(mod);
    return 0;
}

static int method_detach(module_data_t *mod)
{
    stream_ts_detach(mod);
    return 0;
}

/* required */

static void module_init(module_data_t *mod)
{
    log_debug(LOG_MSG("init"));

    /* protocols */
    stream_ts_init(mod, NULL, NULL, NULL, NULL, NULL);

    mod->sock = socket_open(SOCKET_PROTO_UDP | SOCKET_REUSEADDR | SOCKET_BIND
#ifdef _WIN32
                            , NULL
#else
                            , mod->config.addr
#endif
                            , mod->config.port);
    if(mod->config.socket_size > 0)
        socket_set_buffer(mod->sock, mod->config.socket_size, 0);
    event_attach(mod->sock, udp_input_callback, mod, EVENT_READ);
    socket_multicast_join(mod->sock, mod->config.addr, mod->config.local);
}

static void module_destroy(module_data_t *mod)
{
    log_debug(LOG_MSG("destroy"));

    /* protocols */
    stream_ts_destroy(mod);

    if(mod->sock)
    {
        socket_multicast_leave(mod->sock, mod->config.addr);
        event_detach(mod->sock);
        socket_close(mod->sock);
    }
}

MODULE_OPTIONS()
{
    OPTION_STRING("addr"       , config.addr       , 1, NULL)
    OPTION_NUMBER("port"       , config.port       , 0, 1234)
    OPTION_STRING("localaddr"  , config.local      , 0, NULL)
    OPTION_NUMBER("socket_size", config.socket_size, 0, 0)
    OPTION_NUMBER("rtp"        , config.rtp        , 0, 0)
};

MODULE_METHODS()
{
    METHOD(attach)
    METHOD(detach)
};

MODULE(udp_input)
