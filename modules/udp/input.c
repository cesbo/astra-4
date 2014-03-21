/*
 * Astra Module: UDP Input
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
 *      udp_input
 *
 * Module Options:
 *      addr        - string, source IP address
 *      port        - number, source UDP port
 *      localaddr   - string, IP address of the local interface
 *      socket_size - number, socket buffer size
 *      renew       - number, renewing multicast subscription interval in seconds
 */

#include <astra.h>

#define UDP_BUFFER_SIZE 1460
#define TS_PACKET_SIZE 188
#define RTP_HEADER_SIZE 12

#define MSG(_msg) "[udp_input %s:%d] " _msg, mod->addr, mod->port

struct module_data_t
{
    MODULE_LUA_DATA();
    MODULE_STREAM_DATA();

    const char *addr;
    int port;
    const char *localaddr;

    int rtp_skip;

    asc_socket_t *sock;
    asc_timer_t *timer_renew;

    uint8_t buffer[UDP_BUFFER_SIZE];
};

void on_close(void *arg)
{
    module_data_t *mod = (module_data_t *)arg;

    if(mod->sock)
    {
        asc_socket_multicast_leave(mod->sock);
        asc_socket_close(mod->sock);
        mod->sock = NULL;
    }

    if(mod->timer_renew)
    {
        asc_timer_destroy(mod->timer_renew);
        mod->timer_renew = NULL;
    }
}

void on_read(void *arg)
{
    module_data_t *mod = (module_data_t *)arg;

    int len = asc_socket_recv(mod->sock, mod->buffer, UDP_BUFFER_SIZE);
    if(len <= 0)
    {
        if(len == 0 || errno == EAGAIN)
            return;

        on_close(mod);
        return;
    }

    int i = mod->rtp_skip;
    for(; i < len; i += TS_PACKET_SIZE)
        module_stream_send(mod, &mod->buffer[i]);

    if(i != len)
        asc_log_warning(MSG("wrong UDP packet size. drop %d bytes"), len - i);
}

void on_read_check(void *arg)
{
    module_data_t *mod = (module_data_t *)arg;

    ssize_t len = asc_socket_recv(mod->sock, mod->buffer, UDP_BUFFER_SIZE);
    if(len <= 0)
    {
        if(len == 0 || errno == EAGAIN)
            return;

        on_close(mod);
        return;
    }

    bool is_ok = false;

    if(mod->buffer[0] == 0x47 && mod->buffer[TS_PACKET_SIZE] == 0x47)
    {
        is_ok = true;
    }
    else if(   mod->buffer[RTP_HEADER_SIZE] == 0x47
            && mod->buffer[RTP_HEADER_SIZE + TS_PACKET_SIZE] == 0x47)
    {
        mod->rtp_skip = RTP_HEADER_SIZE;
        is_ok = true;
    }

    if(!is_ok)
    {
        asc_log_error(MSG("wrong format"));
        on_close(mod);
    }
    else
    {
        asc_socket_set_on_read(mod->sock, on_read);
    }
}

void timer_renew_callback(void *arg)
{
    module_data_t *mod = arg;
    asc_socket_multicast_renew(mod->sock);
}

static void module_init(module_data_t *mod)
{
    module_stream_init(mod, NULL);

    module_option_string("addr", &mod->addr, NULL);
    if(!mod->addr)
    {
        asc_log_error("[udp_input] option 'addr' is required");
        astra_abort();
    }

    module_option_number("port", &mod->port);
    if(!mod->port)
        mod->port = 1234;

    mod->sock = asc_socket_open_udp4(mod);
    asc_socket_set_reuseaddr(mod->sock, 1);
#ifdef _WIN32
    if(!asc_socket_bind(mod->sock, NULL, mod->port))
#else
    if(!asc_socket_bind(mod->sock, mod->addr, mod->port))
#endif
        return;

    int value;
    if(module_option_number("socket_size", &value))
        asc_socket_set_buffer(mod->sock, value, 0);

    asc_socket_set_on_read(mod->sock, on_read_check);
    asc_socket_set_on_close(mod->sock, on_close);

    module_option_string("localaddr", &mod->localaddr, NULL);
    asc_socket_multicast_join(mod->sock, mod->addr, mod->localaddr);

    if(module_option_number("renew", &value))
        mod->timer_renew = asc_timer_init(value * 1000, timer_renew_callback, mod);
}

static void module_destroy(module_data_t *mod)
{
    module_stream_destroy(mod);

    on_close(mod);
}

MODULE_STREAM_METHODS()

MODULE_LUA_METHODS()
{
    MODULE_STREAM_METHODS_REF()
};
MODULE_LUA_REGISTER(udp_input)
