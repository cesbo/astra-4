/*
 * Astra Core
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2015, Andrey Dyldin <and@cesbo.com>
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

#include "assert.h"
#include "socket.h"
#include "event.h"
#include "log.h"

#ifdef _WIN32
#   include <ws2tcpip.h>
#   define SHUT_RD SD_RECEIVE
#   define SHUT_WR SD_SEND
#   define SHUT_RDWR SD_BOTH
#else
#   include <sys/socket.h>
#   include <arpa/inet.h>
#   include <netinet/in.h>
#   include <netinet/tcp.h>
#   ifdef HAVE_SCTP_H
#       include <netinet/sctp.h>
#   endif
#   include <netdb.h>
#endif

#ifdef IGMP_EMULATION
#   define IP_HEADER_SIZE 24
#   define IGMP_HEADER_SIZE 8
#endif

#define MSG(_msg) "[core/socket %d]" _msg, sock->fd

struct asc_socket_t
{
    int fd;
    int family;
    int type;
    int protocol;

    asc_event_t *event;

    struct sockaddr_in addr;
    struct sockaddr_in sockaddr; /* recvfrom, sendto, set_sockaddr */

    struct ip_mreq mreq;

    /* Callbacks */
    void *arg;
    event_callback_t on_read;      /* data read */
    event_callback_t on_close;     /* error occured (connection closed) */
    event_callback_t on_ready;     /* data send is possible now */
};

/*
 * sending multicast: socket(LOOPBACK) -> set_if() -> sendto() -> close()
 * receiving multicast: socket(REUSEADDR | BIND) -> join() -> read() -> close()
 */

void asc_socket_core_init(void)
{
#ifdef _WIN32
    WSADATA wsaData;
    int err = WSAStartup(MAKEWORD(2, 2), &wsaData);
    asc_assert(err == 0, "[core/socket] WSAStartup failed %d", err);
#else
    (void)0;
#endif
}

void asc_socket_core_destroy(void)
{
#ifdef _WIN32
    WSACleanup();
#else
    ;
#endif
}

const char * asc_socket_error(void)
{
    static char buffer[1024];

#ifdef _WIN32
    const int err = WSAGetLastError();
    char *msg;
    if(FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER
                      | FORMAT_MESSAGE_FROM_SYSTEM
                      | FORMAT_MESSAGE_MAX_WIDTH_MASK
                      , NULL, err, 0, (LPSTR)&msg, sizeof(buffer), NULL))
    {
        snprintf(buffer, sizeof(buffer), "%d: %s", err, msg);
        LocalFree(msg);
    }
    else
        snprintf(buffer, sizeof(buffer), "%d: unknown error", err);
#else
    snprintf(buffer, sizeof(buffer), "%d: %s", errno, strerror(errno));
#endif

    return buffer;
}

/*
 *   ooooooo  oooooooooo ooooooooooo oooo   oooo
 * o888   888o 888    888 888    88   8888o  88
 * 888     888 888oooo88  888ooo8     88 888o88
 * 888o   o888 888        888    oo   88   8888
 *   88ooo88  o888o      o888ooo8888 o88o    88
 *
 */

static asc_socket_t * __socket_open(int family, int type, int protocol, void * arg)
{
    const int fd = socket(family, type, protocol);
    asc_assert(fd != -1, "[core/socket] failed to open socket [%s]", asc_socket_error());
    asc_socket_t *sock = (asc_socket_t *)calloc(1, sizeof(asc_socket_t));
    sock->fd = fd;
    sock->mreq.imr_multiaddr.s_addr = INADDR_NONE;
    sock->family = family;
    sock->type = type;
    sock->protocol = protocol;
    sock->arg = arg;

    asc_socket_set_nonblock(sock, true);
    return sock;
}

asc_socket_t * asc_socket_open_tcp4(void * arg)
{
    return __socket_open(PF_INET, SOCK_STREAM, IPPROTO_TCP, arg);
}

asc_socket_t * asc_socket_open_udp4(void * arg)
{
    return __socket_open(PF_INET, SOCK_DGRAM, IPPROTO_UDP, arg);
}

asc_socket_t * asc_socket_open_sctp4(void * arg)
{
#ifndef IPPROTO_SCTP
    asc_log_error("[core/socket] SCTP protocol is not available");
    astra_abort();
    return NULL;
#else
    return __socket_open(PF_INET, SOCK_STREAM, IPPROTO_SCTP, arg);
#endif
}

/*
 *   oooooooo8 ooooo         ooooooo    oooooooo8 ooooooooooo
 * o888     88  888        o888   888o 888         888    88
 * 888          888        888     888  888oooooo  888ooo8
 * 888o     oo  888      o 888o   o888         888 888    oo
 *  888oooo88  o888ooooo88   88ooo88   o88oooo888 o888ooo8888
 *
 */

void asc_socket_shutdown_recv(asc_socket_t *sock)
{
    shutdown(sock->fd, SHUT_RD);
}

void asc_socket_shutdown_send(asc_socket_t *sock)
{
    shutdown(sock->fd, SHUT_WR);
}

void asc_socket_shutdown_both(asc_socket_t *sock)
{
    shutdown(sock->fd, SHUT_RDWR);
}

void asc_socket_close(asc_socket_t *sock)
{
    if(!sock)
        return;

    if(sock->event)
        asc_event_close(sock->event);

    if(sock->fd > 0)
    {
#ifdef _WIN32
        shutdown(sock->fd, SHUT_RDWR);
#else
        close(sock->fd);
#endif
    }
    sock->fd = 0;
    free(sock);
}

/*
 * ooooooooooo ooooo  oooo ooooooooooo oooo   oooo ooooooooooo
 *  888    88   888    88   888    88   8888o  88  88  888  88
 *  888ooo8      888  88    888ooo8     88 888o88      888
 *  888    oo     88888     888    oo   88   8888      888
 * o888ooo8888     888     o888ooo8888 o88o    88     o888o
 *
 */

static void __asc_socket_on_close(void *arg)
{
    asc_socket_t *sock = (asc_socket_t *)arg;
    if(sock->on_close)
        sock->on_close(sock->arg);
}

static void __asc_socket_on_connect(void *arg)
{
    asc_socket_t *sock = (asc_socket_t *)arg;
    asc_event_set_on_write(sock->event, NULL);
    event_callback_t __on_ready = sock->on_ready;
    sock->on_ready(sock->arg);
    if(__on_ready == sock->on_ready)
        sock->on_ready = NULL;
}

static void __asc_socket_on_accept(void *arg)
{
    asc_socket_t *sock = (asc_socket_t *)arg;
    sock->on_read(sock->arg);
}

static void __asc_socket_on_read(void *arg)
{
    asc_socket_t *sock = (asc_socket_t *)arg;
    if(sock->on_read)
        sock->on_read(sock->arg);
}

static void __asc_socket_on_ready(void *arg)
{
    asc_socket_t *sock = (asc_socket_t *)arg;
    if(sock->on_ready)
        sock->on_ready(sock->arg);
}

static bool __asc_socket_check_event(asc_socket_t *sock)
{
    const bool is_callback = (   sock->on_read != NULL
                              || sock->on_ready != NULL
                              || sock->on_close != NULL);

    if(sock->event == NULL)
    {
        if(is_callback == true)
            sock->event = asc_event_init(sock->fd, sock);
    }
    else
    {
        if(is_callback == false)
        {
            asc_event_close(sock->event);
            sock->event = NULL;
        }
    }

    return (sock->event != NULL);
}

void asc_socket_set_on_read(asc_socket_t *sock, event_callback_t on_read)
{
    if(sock->on_read == on_read)
        return;

    sock->on_read = on_read;

    if(__asc_socket_check_event(sock))
    {
        if(on_read != NULL)
            on_read = __asc_socket_on_read;
        asc_event_set_on_read(sock->event, on_read);
    }
}

void asc_socket_set_on_ready(asc_socket_t * sock, event_callback_t on_ready)
{
    if(sock->on_ready == on_ready)
        return;

    sock->on_ready = on_ready;

    if(__asc_socket_check_event(sock))
    {
        if(on_ready != NULL)
            on_ready = __asc_socket_on_ready;
        asc_event_set_on_write(sock->event, on_ready);
    }
}

void asc_socket_set_on_close(asc_socket_t *sock, event_callback_t on_close)
{
    if(sock->on_close == on_close)
        return;

    sock->on_close = on_close;

    if(__asc_socket_check_event(sock))
    {
        if(on_close != NULL)
            on_close = __asc_socket_on_close;
        asc_event_set_on_error(sock->event, on_close);
    }
}

/*
 * oooooooooo ooooo oooo   oooo ooooooooo
 *  888    888 888   8888o  88   888    88o
 *  888oooo88  888   88 888o88   888    888
 *  888    888 888   88   8888   888    888
 * o888ooo888 o888o o88o    88  o888ooo88
 *
 */

bool asc_socket_bind(asc_socket_t *sock, const char *addr, int port)
{
    memset(&sock->addr, 0, sizeof(sock->addr));
    sock->addr.sin_family = sock->family;
    sock->addr.sin_port = htons(port);
    if(addr) // INADDR_ANY by default
        sock->addr.sin_addr.s_addr = inet_addr(addr);

#if defined(__APPLE__) || defined(__FreeBSD__)
    sock->addr.sin_len = sizeof(struct sockaddr_in);

    if(sock->type == SOCK_DGRAM)
    {
        const int optval = 1;
        socklen_t optlen = sizeof(optval);
        setsockopt(sock->fd, SOL_SOCKET, SO_REUSEPORT, &optval, optlen);
    }
#endif

    if(bind(sock->fd, (struct sockaddr *)&sock->addr, sizeof(sock->addr)) == -1)
    {
        asc_log_error(MSG("bind() to %s:%d failed [%s]"), addr, port, asc_socket_error());
        return false;
    }
    return true;
}

/*
 * ooooo       ooooo  oooooooo8 ooooooooooo ooooooooooo oooo   oooo
 *  888         888  888        88  888  88  888    88   8888o  88
 *  888         888   888oooooo     888      888ooo8     88 888o88
 *  888      o  888          888    888      888    oo   88   8888
 * o888ooooo88 o888o o88oooo888    o888o    o888ooo8888 o88o    88
 *
 */

void asc_socket_listen(  asc_socket_t *sock
                       , event_callback_t on_accept, event_callback_t on_error)
{
    asc_assert(on_accept && on_error, MSG("listen() - on_ok/on_err not specified"));
    if(listen(sock->fd, SOMAXCONN) == -1)
    {
        asc_log_error(MSG("listen() on socket failed [%s]"), asc_socket_error());

        close(sock->fd);
        sock->fd = 0;
        return;
    }

    sock->on_read = on_accept;
    sock->on_ready = NULL;
    sock->on_close = on_error;
    if(sock->event == NULL)
        sock->event = asc_event_init(sock->fd, sock);

    asc_event_set_on_read(sock->event, __asc_socket_on_accept);
    asc_event_set_on_write(sock->event, NULL);
    asc_event_set_on_error(sock->event, __asc_socket_on_close);
}

/*
 *      o       oooooooo8   oooooooo8 ooooooooooo oooooooooo  ooooooooooo
 *     888    o888     88 o888     88  888    88   888    888 88  888  88
 *    8  88   888         888          888ooo8     888oooo88      888
 *   8oooo88  888o     oo 888o     oo  888    oo   888            888
 * o88o  o888o 888oooo88   888oooo88  o888ooo8888 o888o          o888o
 *
 */

bool asc_socket_accept(asc_socket_t *sock, asc_socket_t **client_ptr, void * arg)
{
    asc_socket_t *client = (asc_socket_t *)calloc(1, sizeof(asc_socket_t));
    socklen_t sin_size = sizeof(client->addr);
    client->fd = accept(sock->fd, (struct sockaddr *)&client->addr, &sin_size);
    if(client->fd <= 0)
    {
        asc_log_error(MSG("accept() failed [%s]"), asc_socket_error());
        free(client);
        *client_ptr = NULL;
        return false;
    }

    client->arg = arg;
    asc_socket_set_nonblock(client, true);

    *client_ptr = client;
    return true;
}

/*
 *   oooooooo8   ooooooo  oooo   oooo oooo   oooo ooooooooooo  oooooooo8 ooooooooooo
 * o888     88 o888   888o 8888o  88   8888o  88   888    88 o888     88 88  888  88
 * 888         888     888 88 888o88   88 888o88   888ooo8   888             888
 * 888o     oo 888o   o888 88   8888   88   8888   888    oo 888o     oo     888
 *  888oooo88    88ooo88  o88o    88  o88o    88  o888ooo8888 888oooo88     o888o
 *
 */

void asc_socket_connect(  asc_socket_t *sock, const char *addr, int port
                        , event_callback_t on_connect, event_callback_t on_error)
{
    asc_assert(on_connect && on_error, MSG("connect() - on_ok/on_err not specified"));
    memset(&sock->addr, 0, sizeof(sock->addr));
    sock->addr.sin_family = sock->family;
    // sock->addr.sin_addr.s_addr = inet_addr(addr);
    sock->addr.sin_port = htons(port);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = sock->type;
    hints.ai_family = sock->family;
    int err = getaddrinfo(addr, NULL, &hints, &res);
    if(err == 0)
    {
        memcpy(&sock->addr.sin_addr
               , &((struct sockaddr_in *)res->ai_addr)->sin_addr
               , sizeof(sock->addr.sin_addr));
        freeaddrinfo(res);
    }
    else
    {
        asc_log_error(MSG("getaddrinfo() failed '%s' [%s])"), addr, gai_strerror(err));

        close(sock->fd);
        sock->fd = 0;
        return;
    }

    if(connect(sock->fd, (struct sockaddr *)&sock->addr, sizeof(sock->addr)) == -1)
    {
#ifdef _WIN32
        const int err = WSAGetLastError();
        const bool is_error = (err == WSAEWOULDBLOCK) || (err == WSAEINPROGRESS);
#else
        const bool is_error = (errno == EISCONN) || (errno == EINPROGRESS) || (errno == EAGAIN);
#endif
        if(!is_error)
        {
            asc_log_error(MSG("connect() to %s:%d failed [%s]"), addr, port, asc_socket_error());

            close(sock->fd);
            sock->fd = 0;
            return;
        }
    }

    sock->on_read = NULL;
    sock->on_ready = on_connect;
    sock->on_close = on_error;
    if(sock->event == NULL)
        sock->event = asc_event_init(sock->fd, sock);

    asc_event_set_on_read(sock->event, NULL);
    asc_event_set_on_write(sock->event, __asc_socket_on_connect);
    asc_event_set_on_error(sock->event, __asc_socket_on_close);
}

/*
 * oooooooooo  ooooooooooo  oooooooo8 ooooo  oooo
 *  888    888  888    88 o888     88  888    88
 *  888oooo88   888ooo8   888           888  88
 *  888  88o    888    oo 888o     oo    88888
 * o888o  88o8 o888ooo8888 888oooo88      888
 *
 */

ssize_t asc_socket_recv(asc_socket_t *sock, void *buffer, size_t size)
{
    return recv(sock->fd, buffer, size, 0);
}

ssize_t asc_socket_recvfrom(asc_socket_t *sock, void *buffer, size_t size)
{
    socklen_t slen = sizeof(struct sockaddr_in);
    return recvfrom(sock->fd, buffer, size, 0, (struct sockaddr *)&sock->sockaddr, &slen);
}

/*
 *  oooooooo8 ooooooooooo oooo   oooo ooooooooo
 * 888         888    88   8888o  88   888    88o
 *  888oooooo  888ooo8     88 888o88   888    888
 *         888 888    oo   88   8888   888    888
 * o88oooo888 o888ooo8888 o88o    88  o888ooo88
 *
 */

ssize_t asc_socket_send(asc_socket_t *sock, const void *buffer, size_t size)
{
    const ssize_t ret = send(sock->fd, buffer, size, 0);
    if(ret == -1)
    {
#ifdef _WIN32
        const int err = WSAGetLastError();
        if(err == WSAEWOULDBLOCK)
            return 0;
#else
        if(errno == EAGAIN)
            return 0;
#endif
    }
    return ret;
}

ssize_t asc_socket_sendto(asc_socket_t *sock, const void *buffer, size_t size)
{
    socklen_t slen = sizeof(struct sockaddr_in);
    return sendto(sock->fd, buffer, size, 0, (struct sockaddr *)&sock->sockaddr, slen);
}

/*
 * ooooo oooo   oooo ooooooooooo  ooooooo
 *  888   8888o  88   888    88 o888   888o
 *  888   88 888o88   888ooo8   888     888
 *  888   88   8888   888       888o   o888
 * o888o o88o    88  o888o        88ooo88
 *
 */

inline int asc_socket_fd(asc_socket_t *sock)
{
    return sock->fd;
}

const char * asc_socket_addr(asc_socket_t *sock)
{
    return inet_ntoa(sock->addr.sin_addr);
}

int asc_socket_port(asc_socket_t *sock)
{
    struct sockaddr_in s;
    socklen_t slen = sizeof(s);
    if(getsockname(sock->fd, (struct sockaddr *)&s, &slen) != -1)
        return htons(s.sin_port);
    return -1;
}

/*
 *  oooooooo8 ooooooooooo ooooooooooo          oo    oo
 * 888         888    88  88  888  88           88oo88
 *  888oooooo  888ooo8        888     ooooooo o88888888o
 *         888 888    oo      888               oo88oo
 * o88oooo888 o888ooo8888    o888o             o88  88o
 *
 */

void asc_socket_set_nonblock(asc_socket_t *sock, bool is_nonblock)
{
    if(is_nonblock == false && sock->event)
    {
        sock->on_read = NULL;
        sock->on_ready = NULL;
        sock->on_close = NULL;

        asc_event_close(sock->event);
        sock->event = NULL;
    }

#ifdef _WIN32
    unsigned long nonblock = (is_nonblock == true) ? 1 : 0;
    if(ioctlsocket(sock->fd, FIONBIO, &nonblock) != NO_ERROR)
#else
    int flags = (is_nonblock == true)
              ? (fcntl(sock->fd, F_GETFL) | O_NONBLOCK)
              : (fcntl(sock->fd, F_GETFL) & ~O_NONBLOCK);
    if(fcntl(sock->fd, F_SETFL, flags) == -1)
#endif
    {
        asc_log_error(MSG("failed to set NONBLOCK [%s]"), asc_socket_error());
        astra_abort();
    }
}

void asc_socket_set_sockaddr(asc_socket_t *sock, const char *addr, int port)
{
    memset(&sock->sockaddr, 0, sizeof(sock->sockaddr));
    sock->sockaddr.sin_family = sock->family;
    sock->sockaddr.sin_addr.s_addr = (addr) ? inet_addr(addr) : INADDR_ANY;
    sock->sockaddr.sin_port = htons(port);
}

void asc_socket_set_reuseaddr(asc_socket_t *sock, int is_on)
{
    setsockopt(sock->fd, SOL_SOCKET, SO_REUSEADDR, (void *)&is_on, sizeof(is_on));
}

void asc_socket_set_non_delay(asc_socket_t *sock, int is_on)
{
    switch(sock->protocol)
    {
#ifdef IPPROTO_SCTP
        case IPPROTO_SCTP:
#ifdef SCTP_NODELAY
            setsockopt(sock->fd, sock->protocol, SCTP_NODELAY, (void *)&is_on, sizeof(is_on));
#else
            asc_log_error("[core/socket] SCTP_NODELAY is not available");
#endif
            break;
#endif
        case IPPROTO_TCP:
            setsockopt(sock->fd, sock->protocol, TCP_NODELAY, (void *)&is_on, sizeof(is_on));
            break;
        default:
            break;
    }
}

void asc_socket_set_keep_alive(asc_socket_t *sock, int is_on)
{
    setsockopt(sock->fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&is_on, is_on);
}

void asc_socket_set_broadcast(asc_socket_t *sock, int is_on)
{
    setsockopt(sock->fd, SOL_SOCKET, SO_BROADCAST, (void *)&is_on, sizeof(is_on));
}

void asc_socket_set_timeout(asc_socket_t *sock, int rcvmsec, int sndmsec)
{
#ifdef _WIN32
    if(rcvmsec > 0)
        setsockopt(sock->fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&rcvmsec, sizeof(rcvmsec));
    if(sndmsec > 0)
        setsockopt(sock->fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&sndmsec, sizeof(sndmsec));
#else
    struct timeval tv;

    if(rcvmsec > 0)
    {
        tv.tv_sec = rcvmsec / 1000;
        tv.tv_usec = rcvmsec % 1000;
        setsockopt(sock->fd, SOL_SOCKET, SO_RCVTIMEO, (void *)&tv, sizeof(tv));
    }

    if(sndmsec > 0)
    {
        if(rcvmsec != sndmsec)
        {
            tv.tv_sec = sndmsec / 1000;
            tv.tv_usec = sndmsec % 1000;
        }
        setsockopt(sock->fd, SOL_SOCKET, SO_SNDTIMEO, (void *)&tv, sizeof(tv));
    }
#endif
}

static int _socket_set_buffer(int fd, int type, int size)
{
    int val;
    socklen_t slen = sizeof(val);
#ifdef __linux__
    val = size / 2;
#else
    val = size;
#endif
    setsockopt(fd, SOL_SOCKET, type, (void *)&val, slen);
    val = 0;
    getsockopt(fd, SOL_SOCKET, type, (void *)&val, &slen);
    return (slen && val == size);
}

void asc_socket_set_buffer(asc_socket_t *sock, int rcvbuf, int sndbuf)
{
#if defined(SO_RCVBUF) && defined(SO_SNDBUF)
    if(rcvbuf > 0 && !_socket_set_buffer(sock->fd, SO_RCVBUF, rcvbuf))
    {
        asc_log_error(MSG("failed to set rcvbuf %d (%s)"), rcvbuf, asc_socket_error());
    }

    if(sndbuf > 0 && !_socket_set_buffer(sock->fd, SO_SNDBUF, rcvbuf))
    {
        asc_log_error(MSG("failed to set sndbuf %d (%s)"), sndbuf, asc_socket_error());
    }
#else
#   warning "RCVBUF/SNDBUF is not defined"
#endif
}

/*
 * oooo     oooo       oooooooo8     o       oooooooo8 ooooooooooo
 *  8888o   888      o888     88    888     888        88  888  88
 *  88 888o8 88      888           8  88     888oooooo     888
 *  88  888  88  ooo 888o     oo  8oooo88           888    888
 * o88o  8  o88o 888  888oooo88 o88o  o888o o88oooo888    o888o
 *
 */

#ifdef IGMP_EMULATION
static uint16_t in_chksum(uint8_t *buffer, int size)
{
    uint16_t *_buffer = (uint16_t *)buffer;
    register int nleft = size;
    register int sum = 0;
    uint16_t answer = 0;

    while(nleft > 1)
    {
        sum += *_buffer;
        ++_buffer;
        nleft -= 2;
    }

    if(nleft == 1)
    {
        *(uint8_t *)(&answer) = *(uint8_t *)_buffer;
        sum += answer;
    }

    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    answer = ~sum;
    return htons(answer);
}

static void create_igmp_packet(uint8_t *buffer, uint8_t igmp_type, uint32_t dst_addr)
{
    // IP Header
    buffer[0] = (4 << 4) | (6); // Version | IHL
    buffer[1] = 0xC0; // TOS
    // Total length
    const uint16_t total_length = IP_HEADER_SIZE + IGMP_HEADER_SIZE;
    buffer[2] = (total_length >> 8) & 0xFF;
    buffer[3] = (total_length) & 0xFF;
    // ID: buffer[4], buffer[5]
    // Fragmen offset: buffer[6], buffer[7]
    buffer[8] = 1; // TTL
    buffer[9] = IPPROTO_IGMP; // Protocol
    // Checksum
    const uint16_t ip_checksum = in_chksum(buffer, IP_HEADER_SIZE);
    buffer[10] = (ip_checksum >> 8) & 0xFF;
    buffer[11] = (ip_checksum) & 0xFF;
    // Source address
    const uint32_t _src_addr = htonl(INADDR_ANY);
    buffer[12] = (_src_addr >> 24) & 0xFF;
    buffer[13] = (_src_addr >> 16) & 0xFF;
    buffer[14] = (_src_addr >> 8) & 0xFF;
    buffer[15] = (_src_addr) & 0xFF;
    // Destination address
    const uint32_t _dst_addr = htonl(dst_addr);
    buffer[16] = (_dst_addr >> 24) & 0xFF;
    buffer[17] = (_dst_addr >> 16) & 0xFF;
    buffer[18] = (_dst_addr >> 8) & 0xFF;
    buffer[19] = (_dst_addr) & 0xFF;
    // Options
    buffer[20] = 0x94;
    buffer[21] = 0x04;
    buffer[22] = 0x00;
    buffer[23] = 0x00;

    // IGMP
    buffer[24] = igmp_type; // Type
    buffer[25] = 0; // Max resp time

    buffer[28] = (_dst_addr >> 24) & 0xFF;
    buffer[29] = (_dst_addr >> 16) & 0xFF;
    buffer[30] = (_dst_addr >> 8) & 0xFF;
    buffer[31] = (_dst_addr) & 0xFF;

    const uint16_t igmp_checksum = in_chksum(&buffer[IP_HEADER_SIZE], IGMP_HEADER_SIZE);
    buffer[26] = (igmp_checksum >> 8) & 0xFF;
    buffer[27] = (igmp_checksum) & 0xFF;
}
#endif

void asc_socket_set_multicast_if(asc_socket_t *sock, const char *addr)
{
    if(!addr)
        return;
    struct in_addr a;
    a.s_addr = inet_addr(addr);
    if(setsockopt(sock->fd, IPPROTO_IP, IP_MULTICAST_IF, (void *)&a, sizeof(a)) == -1)
    {
        asc_log_error(MSG("failed to set if \"%s\" (%s)"), addr, asc_socket_error());
    }
}

void asc_socket_set_multicast_ttl(asc_socket_t *sock, int ttl)
{
    if(ttl <= 0)
        return;
    if(setsockopt(sock->fd, IPPROTO_IP, IP_MULTICAST_TTL, (void *)&ttl, sizeof(ttl)) == -1)
    {
        asc_log_error(MSG("failed to set ttl \"%d\" (%s)"), ttl, asc_socket_error());
    }
}

void asc_socket_set_multicast_loop(asc_socket_t *sock, int is_on)
{
    setsockopt(sock->fd, IPPROTO_IP, IP_MULTICAST_LOOP, (void *)&is_on, sizeof(is_on));
}

/* multicast_* */

void asc_socket_multicast_join(asc_socket_t *sock, const char *addr, const char *localaddr)
{
    memset(&sock->mreq, 0, sizeof(sock->mreq));
    sock->mreq.imr_multiaddr.s_addr = inet_addr(addr);
    if(sock->mreq.imr_multiaddr.s_addr == INADDR_NONE)
    {
        asc_log_error(MSG("failed to join multicast \"%s\" (%s)"), addr, asc_socket_error());
        return;
    }
    if(!IN_MULTICAST(ntohl(sock->mreq.imr_multiaddr.s_addr)))
    {
        sock->mreq.imr_multiaddr.s_addr = INADDR_NONE;
        return;
    }
    if(localaddr)
    {
        sock->mreq.imr_interface.s_addr = inet_addr(localaddr);
        if(sock->mreq.imr_interface.s_addr == INADDR_NONE)
            sock->mreq.imr_interface.s_addr = INADDR_ANY;
    }
#ifndef IGMP_EMULATION
    int r = setsockopt(sock->fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
        (void *)&sock->mreq, sizeof(sock->mreq));
#else
    uint8_t buffer[IP_HEADER_SIZE + IGMP_HEADER_SIZE];
    memset(buffer, 0, IP_HEADER_SIZE + IGMP_HEADER_SIZE);

    struct sockaddr_in dst;
    dst.sin_addr.s_addr = sock->mreq.imr_multiaddr.s_addr;
    dst.sin_family = AF_INET;

    create_igmp_packet(buffer, 0x16, sock->mreq.imr_multiaddr.s_addr);

    int raw_sock = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if(raw_sock == -1)
        return;

    int r = sendto(raw_sock, &buffer, IP_HEADER_SIZE + IGMP_HEADER_SIZE, 0,
        (struct sockaddr *)&dst, sizeof(struct sockaddr_in));

    close(raw_sock);
#endif

    if(r == -1)
    {
        asc_log_error(MSG("failed to join multicast \"%s\" (%s)"),
            inet_ntoa(sock->mreq.imr_multiaddr), asc_socket_error());
        sock->mreq.imr_multiaddr.s_addr = INADDR_NONE;
    }
}

void asc_socket_multicast_leave(asc_socket_t *sock)
{
    if(sock->mreq.imr_multiaddr.s_addr == INADDR_NONE)
        return;

#ifndef IGMP_EMULATION
    int r = setsockopt(sock->fd, IPPROTO_IP, IP_DROP_MEMBERSHIP,
        (void *)&sock->mreq, sizeof(sock->mreq));
#else
    uint8_t buffer[IP_HEADER_SIZE + IGMP_HEADER_SIZE];
    memset(buffer, 0, IP_HEADER_SIZE + IGMP_HEADER_SIZE);

    struct sockaddr_in dst;
    dst.sin_addr.s_addr = sock->mreq.imr_multiaddr.s_addr;
    dst.sin_family = AF_INET;

    create_igmp_packet(buffer, 0x17, sock->mreq.imr_multiaddr.s_addr);

    int raw_sock = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if(raw_sock == -1)
        return;

    int r = sendto(raw_sock, &buffer, IP_HEADER_SIZE + IGMP_HEADER_SIZE, 0,
        (struct sockaddr *)&dst, sizeof(struct sockaddr_in));

    close(raw_sock);
#endif

    if(r == -1)
    {
        asc_log_error(MSG("failed to leave multicast \"%s\" (%s)"),
            inet_ntoa(sock->mreq.imr_multiaddr), asc_socket_error());
    }
}

void asc_socket_multicast_renew(asc_socket_t *sock)
{
    if(sock->mreq.imr_multiaddr.s_addr == INADDR_NONE)
        return;

#ifndef IGMP_EMULATION
    int r = setsockopt(sock->fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
        (void *)&sock->mreq, sizeof(sock->mreq));
#else
    uint8_t buffer[IP_HEADER_SIZE + IGMP_HEADER_SIZE];
    memset(buffer, 0, IP_HEADER_SIZE + IGMP_HEADER_SIZE);

    struct sockaddr_in dst;
    dst.sin_addr.s_addr = sock->mreq.imr_multiaddr.s_addr;
    dst.sin_family = AF_INET;

    create_igmp_packet(buffer, 0x16, sock->mreq.imr_multiaddr.s_addr);

    int raw_sock = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if(raw_sock == -1)
        return;

    int r = sendto(raw_sock, &buffer, IP_HEADER_SIZE + IGMP_HEADER_SIZE, 0,
        (struct sockaddr *)&dst, sizeof(struct sockaddr_in));

    close(raw_sock);
#endif

    if(r == 1)
    {
        asc_log_error(MSG("failed to renew multicast \"%s\" (%s)"),
            inet_ntoa(sock->mreq.imr_multiaddr), asc_socket_error());
    }
}
