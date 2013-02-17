/*
 * Astra UDP Module
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

/*
 * Module Name:
 *      udp_input
 *
 * Module Options:
 *      addr        - string, source IP address
 *      port        - number, source UDP port
 *      localaddr   - string, IP address of the local interface
 *      socket_size - number, socket buffer size
 *      rtp         - boolean, use RTP instad RAW UDP
 *      renew       - number, renewing multicast subscription interval in seconds
 */

#include <astra.h>

#define UDP_BUFFER_SIZE 1460
#define TS_PACKET_SIZE 188

struct module_data_s
{
    MODULE_STREAM_BASE();

    int is_rtp;

    socket_t *sock;
    timer_t *timer_renew;

    uint8_t buffer[UDP_BUFFER_SIZE];
};

void udp_input_callback(void *arg, int event)
{
    module_data_t *mod = (module_data_t *)arg;

    if(event == EVENT_ERROR)
    {
        socket_close(mod->sock);
        mod->sock = NULL;
        return;
    }

    ssize_t len = socket_recv(mod->sock, mod->buffer, UDP_BUFFER_SIZE);
    if(len <= 0)
    {
        udp_input_callback(arg, EVENT_ERROR);
        return;
    }

    // 12 - RTP header size
    ssize_t i = (mod->is_rtp) ? 12 : 0;
    for(; i < len; i += TS_PACKET_SIZE)
        module_stream_send(mod, &mod->buffer[i]);
}

void timer_renew_callback(void *arg)
{
    module_data_t *mod = arg;
    socket_multicast_renew(mod->sock);
}

/* required */

static void module_initialize(module_data_t *mod)
{
    module_stream_api_t api = { .on_ts = NULL };
    module_stream_init(mod, &api);

    const char *addr;
    if(!module_option_string("addr", &addr))
    {
        log_error("[udp_input] option 'addr' is required");
        astra_abort();
    }

    int port = 1234;
    module_option_number("port", &port);

    mod->sock = socket_open_udp4();
    socket_set_reuseaddr(mod->sock, 1);
    if(!socket_bind(mod->sock, addr, port))
        return;

    int value;
    if(module_option_number("socket_size", &value))
        socket_set_buffer(mod->sock, value, 0);

    socket_event_attach(mod->sock, EVENT_READ, udp_input_callback, mod);

    const char *localaddr = NULL;
    module_option_string("localaddr", &localaddr);
    socket_multicast_join(mod->sock, addr, localaddr);

    if(module_option_number("renew", &value))
        mod->timer_renew = timer_attach(value * 1000, timer_renew_callback, mod);
}

static void module_destroy(module_data_t *mod)
{
    module_stream_destroy(mod);

    if(mod->timer_renew)
        timer_detach(mod->timer_renew);

    if(mod->sock)
    {
        socket_multicast_leave(mod->sock);
        socket_close(mod->sock);
    }
}

MODULE_METHODS()
{
    MODULE_STREAM_METHODS()
};

MODULE_LUA_REGISTER(udp_input)
