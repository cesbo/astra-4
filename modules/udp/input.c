/*
 * Astra Module: UDP Input
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
 *      udp_input
 *
 * Module Options:
 *      addr        - string, source IP address
 *      port        - number, source UDP port
 *      localaddr   - string, IP address of the local interface
 *      socket_size - number, socket buffer size
 *      renew       - number, renewing multicast subscription interval in seconds
 *      rtp         - boolean, use RTP instad RAW UDP
 *
 * Module Methods:
 *      port()      - return number, random port number
 */

#include <astra.h>

#define UDP_BUFFER_SIZE 1460
#define RTP_HEADER_SIZE 12

#define MSG(_msg) "[udp_input %s:%d] " _msg, mod->addr, mod->port

struct module_data_t
{
    MODULE_STREAM_DATA();

    const char *addr;
    int port;
    const char *localaddr;

    bool is_error_message;
    int rtp_skip;

    asc_socket_t *sock;
    asc_timer_t *timer_renew;

    uint8_t buffer[UDP_BUFFER_SIZE];
};

static void on_close(void *arg)
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

static void on_read(void *arg)
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
    for(; i <= len - TS_PACKET_SIZE; i += TS_PACKET_SIZE)
        module_stream_send(mod, &mod->buffer[i]);

    if(i != len && !mod->is_error_message)
    {
        asc_log_error(MSG("wrong stream format. drop %d bytes"), len - i);
        mod->is_error_message = true;
    }
}

static void timer_renew_callback(void *arg)
{
    module_data_t *mod = (module_data_t *)arg;
    asc_socket_multicast_renew(mod->sock);
}

static int method_port(module_data_t *mod)
{
    const int port = asc_socket_port(mod->sock);
    lua_pushnumber(lua, port);
    return 1;
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

    bool rtp = false;
    module_option_boolean("rtp", &rtp);
    if(rtp)
        mod->rtp_skip = RTP_HEADER_SIZE;

    asc_socket_set_on_read(mod->sock, on_read);
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
    MODULE_STREAM_METHODS_REF(),
    { "port", method_port },
};
MODULE_LUA_REGISTER(udp_input)
