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

struct module_data_t
{
    MODULE_LUA_DATA();
    MODULE_STREAM_DATA();
    MODULE_DEMUX_DATA();

    int is_rtp;

    asc_socket_t *sock;
    asc_timer_t *timer_renew;

    uint8_t buffer[UDP_BUFFER_SIZE];
};

void on_close(void *arg)
{
    module_data_t *mod = (module_data_t *)arg;
    asc_socket_close(mod->sock);
    mod->sock = NULL;
}

void on_read(void *arg)
{
    module_data_t *mod = (module_data_t *)arg;

    ssize_t len = asc_socket_recv(mod->sock, mod->buffer, UDP_BUFFER_SIZE);
    if(len <= 0)
    {
        on_close(arg);
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
    asc_socket_multicast_renew(mod->sock);
}

static void module_init(module_data_t *mod)
{
    module_stream_init(mod, NULL);
    module_demux_init(mod, NULL, NULL);

    const char *addr = NULL;
    module_option_string("addr", &addr);
    if(!addr)
    {
        asc_log_error("[udp_input] option 'addr' is required");
        astra_abort();
    }

    int port = 1234;
    module_option_number("port", &port);

    mod->sock = asc_socket_open_udp4(mod);
    asc_socket_set_reuseaddr(mod->sock, 1);
#ifdef _WIN32
    if(!asc_socket_bind(mod->sock, NULL, port))
#else
    if(!asc_socket_bind(mod->sock, addr, port))
#endif
        return;

    int value;
    if(module_option_number("socket_size", &value))
        asc_socket_set_buffer(mod->sock, value, 0);

    asc_socket_set_on_read(mod->sock, on_read);
    asc_socket_set_on_close(mod->sock, on_close);

    const char *localaddr = NULL;
    module_option_string("localaddr", &localaddr);
    asc_socket_multicast_join(mod->sock, addr, localaddr);

    if(module_option_number("renew", &value))
        mod->timer_renew = asc_timer_init(value * 1000, timer_renew_callback, mod);
}

static void module_destroy(module_data_t *mod)
{
    module_stream_destroy(mod);
    module_demux_destroy(mod);

    if(mod->timer_renew)
        asc_timer_destroy(mod->timer_renew);

    if(mod->sock)
    {
        asc_socket_multicast_leave(mod->sock);
        asc_socket_close(mod->sock);
    }
}

MODULE_STREAM_METHODS()
MODULE_DEMUX_METHODS()
MODULE_LUA_METHODS()
{
    MODULE_STREAM_METHODS_REF(),
    MODULE_DEMUX_METHODS_REF()
};
MODULE_LUA_REGISTER(udp_input)
