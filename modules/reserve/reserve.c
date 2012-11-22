/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#include <astra.h>

#define UDP_BUFFER_SIZE 1460
#define TS_PACKET_SIZE 188

#define LOG_MSG(_msg) "[reserve %s:%d] " _msg \
                      , mod->config.addr, mod->config.port

struct module_data_s
{
    MODULE_BASE();

    struct
    {
        const char *addr;
        int port;
    } config;

    int is_reserve;

    int sock;
    uint8_t buffer[UDP_BUFFER_SIZE];
};

void reserve_input_callback(void *arg, int event)
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
        reserve_input_callback(arg, EVENT_ERROR);
        return;
    }

    for(int i = 0; i < len; i += TS_PACKET_SIZE)
        stream_ts_send(mod, &mod->buffer[i]);
}

static void callback_send_ts(module_data_t *mod, uint8_t *ts)
{
    if(mod->is_reserve)
        return;

    stream_ts_send(mod, ts);
}

/* methods */

static int method_start(module_data_t *mod)
{
    if(!mod->sock && mod->config.port)
    {
        mod->sock = socket_open(SOCKET_PROTO_UDP
                                | SOCKET_REUSEADDR
                                | SOCKET_BIND
#ifdef _WIN32
                                , NULL
#else
                                , mod->config.addr
#endif
                                , mod->config.port);
        if(mod->sock)
        {
            event_attach(mod->sock, reserve_input_callback, mod, EVENT_READ);
            socket_multicast_join(mod->sock, mod->config.addr, NULL);
        }
    }
    mod->is_reserve = 1;
    return 0;
}

static int method_stop(module_data_t *mod)
{
    if(mod->sock)
    {
        socket_multicast_leave(mod->sock, mod->config.addr, NULL);
        event_detach(mod->sock);
        socket_close(mod->sock);
        mod->sock = 0;
    }
    mod->is_reserve = 0;
    return 0;
}

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

static void module_configure(module_data_t *mod)
{
    module_set_string(mod, "addr", 1, NULL, &mod->config.addr);

    if(!strcasecmp(mod->config.addr, "DROP"))
        return;

    module_set_number(mod, "port", 0, 1234, &mod->config.port);
}

static void module_initialize(module_data_t *mod)
{
    module_configure(mod);

    /* protocols */
    stream_ts_init(mod, callback_send_ts, NULL, NULL, NULL, NULL);
}

static void module_destroy(module_data_t *mod)
{
    /* protocols */
    stream_ts_destroy(mod);

    if(mod->sock)
    {
        socket_multicast_leave(mod->sock, mod->config.addr, NULL);
        event_detach(mod->sock);
        socket_close(mod->sock);
    }
}

MODULE_METHODS()
{
    METHOD(attach)
    METHOD(detach)
    METHOD(start)
    METHOD(stop)
};

MODULE(reserve)
