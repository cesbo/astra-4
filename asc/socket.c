/*
 * AsC Framework
 * http://cesbo.com
 *
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#define ASC
#include "asc.h"

#ifdef _WIN32
#   define _WIN32_WINNT 0x0501
#   include <windows.h>
#   include <winsock2.h>
#   include <ws2tcpip.h>
#else
#   include <sys/socket.h>
#   include <arpa/inet.h>
#   include <netinet/in.h>
#   include <netinet/tcp.h>
#   include <fcntl.h>
#   include <netdb.h>
#endif

#define LOG_MSG(_msg) "[core/socket %d] " _msg, sock

/*
 * sending multicast: socket(LOOPBACK) -> set_if() -> sendto() -> close()
 * receiving multicast: socket(REUSEADDR | BIND) -> join() -> read() -> close()
 */

void socket_init(void)
{
#ifdef _WIN32
    WSADATA wsaData;
    int err = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if(err != 0)
    {
        const int sock = 0; // for LOG_MSG
        log_error(LOG_MSG("WSAStartup failed %d"), err);
        return;
    }
#else
    ;
#endif
}

void socket_destroy(void)
{
#ifdef _WIN32
    WSACleanup();
#else
    ;
#endif
}

char * socket_error(void)
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

int socket_open(int options, const char *addr, int port)
{
    /* defaults */
    if(!options)
        options = (SOCKET_REUSEADDR | SOCKET_BIND);

    if(!(options & (SOCKET_FAMILY_IPv4 | SOCKET_FAMILY_IPv6)))
        options |= SOCKET_FAMILY_IPv4;

    if(!(options & (SOCKET_PROTO_TCP | SOCKET_PROTO_UDP)))
        options |= SOCKET_PROTO_TCP;

    int family, type, proto;
    family = (options & SOCKET_FAMILY_IPv6) ? PF_INET6 : PF_INET;

    if(options & SOCKET_PROTO_UDP)
    {
        type = SOCK_DGRAM;
        proto = IPPROTO_UDP;
    }
    else
    {
        type = SOCK_STREAM;
        proto = IPPROTO_TCP;
    }

    int sock = socket(family, type, proto);

    if(sock <= 0)
    {
        log_error(LOG_MSG("failed to open socket [%s]"), socket_error());
        return 0;
    }

    if(options & SOCKET_REUSEADDR)
    {
        int reuse = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR
                   , (void *)&reuse, sizeof(reuse));
    }

    socket_options_set(sock, options);

    if(options & SOCKET_BIND)
    {
        struct sockaddr_in saddr;
        memset(&saddr, 0, sizeof(saddr));
        saddr.sin_family = family;
        saddr.sin_port = htons(port);
        if(addr) // INADDR_ANY by default
            saddr.sin_addr.s_addr = inet_addr(addr);

        if(bind(sock, (struct sockaddr *)&saddr, sizeof(saddr)) < 0)
        {
            log_error(LOG_MSG("bind() to %s:%d failed [%s]")
                      , (addr) ? addr : "0.0.0.0", port, socket_error());
            socket_close(sock);
            return 0;
        }

        if(options & SOCKET_PROTO_TCP)
        {
            if(listen(sock, INT32_MAX) < 0)
            {
                log_error(LOG_MSG("listen() on %s:%d failed [%s]")
                          , (addr) ? addr : "0.0.0.0", port, socket_error());
                socket_close(sock);
                return 0;
            }
        }
    }
    else if(options & SOCKET_CONNECT)
    {
        struct sockaddr_in saddr;
        memset(&saddr, 0, sizeof(saddr));
        saddr.sin_family = family;
        saddr.sin_port = htons(port);

        struct addrinfo hints, *res;
        memset(&hints, 0, sizeof(hints));
        hints.ai_socktype = type;
        hints.ai_family = family;
        int err = getaddrinfo(addr, NULL, &hints, &res);
        if(err != 0)
        {
            log_error(LOG_MSG("getaddrinfo() failed '%s' [%s])")
                      , addr, gai_strerror(err));
            close(sock);
            return 0;
        }
        memcpy(&saddr.sin_addr
               , &((struct sockaddr_in *)res->ai_addr)->sin_addr
               , sizeof(saddr.sin_addr));
        freeaddrinfo(res);

        if(connect(sock, (struct sockaddr *)&saddr, sizeof(saddr)) < 0)
        {
#ifdef _WIN32
            const int err = WSAGetLastError();
            if((err != WSAEWOULDBLOCK) && (err != WSAEINPROGRESS))
#else
            if((errno != EISCONN) && (errno != EINPROGRESS))
#endif
            {
                log_error(LOG_MSG("connect() to %s:%d failed [%s]")
                          , addr, port, socket_error());
                socket_close(sock);
                return 0;
            }
        }
    }

    return sock;
}

int socket_shutdown(int sock, int how)
{
#ifdef _WIN32
#   define SHUT_RD SD_RECEIVE
#   define SHUT_WR SD_SEND
#   define SHUT_RDWR SD_BOTH
#endif

    switch(how)
    {
        case SOCKET_SHUTDOWN_RECV:
            return shutdown(sock, SHUT_RD);
        case SOCKET_SHUTDOWN_SEND:
            return shutdown(sock, SHUT_WR);
        case SOCKET_SHUTDOWN_BOTH:
            return shutdown(sock, SHUT_RDWR);
    }

    return -1;
}

void socket_close(int sock)
{
    if(sock <= 0)
        return;

#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

int socket_options_set(int sock, int options)
{
    if(!(options & SOCKET_BLOCK))
    {
#ifdef _WIN32
        unsigned long nonblock = 1;
        if(ioctlsocket(sock, FIONBIO, &nonblock) != NO_ERROR)
#else
        int flags = fcntl(sock, F_GETFL);
        if(fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1)
#endif
        {
            log_error(LOG_MSG("failed to set NONBLOCK [%s]"), socket_error());
            socket_close(sock);
            return 0;
        }
    }

    if(options & SOCKET_PROTO_TCP)
    {
        if(options & SOCKET_NO_DELAY)
        {
            const int nodelay = 1;
            setsockopt(sock, IPPROTO_TCP, TCP_NODELAY
                       , (void *)&nodelay, sizeof(nodelay));
        }

        if(options & SOCKET_KEEP_ALIVE)
        {
            const int keepalive = 1;
            setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE
                       , (void *)&keepalive, keepalive);
        }
    }
    else if(options & SOCKET_PROTO_UDP)
    {
        if(options & SOCKET_BROADCAST)
        {
            const int bcast = 1;
            setsockopt(sock, SOL_SOCKET, SO_BROADCAST
                       , (void *)&bcast, sizeof(bcast));
        }

        if(options & SOCKET_LOOP_DISABLE)
        {
            char l = 0;
            setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP
                       , (void *)&l, sizeof(l));
        }
    }

    return 1;
}

int socket_port(int sock)
{
    struct sockaddr_in saddr;
    socklen_t addrlen = sizeof(saddr);
    if(!getsockname(sock, (struct sockaddr *)&saddr, &addrlen)
       && saddr.sin_family == AF_INET
       && addrlen == sizeof(saddr))
    {
        return ntohs(saddr.sin_port);
    }

    return -1;
}

void * socket_sockaddr_init(const char *addr, int port)
{
    struct sockaddr_in *sockaddr
        = (struct sockaddr_in *)calloc(1, sizeof(struct sockaddr_in));
    sockaddr->sin_family = AF_INET;
    sockaddr->sin_addr.s_addr = (addr) ? inet_addr(addr) : INADDR_ANY;
    sockaddr->sin_port = htons(port);
    return sockaddr;
}

void socket_sockaddr_destroy(void *sockaddr)
{
    if(!sockaddr)
        return;

    free(sockaddr);
}

inline ssize_t socket_recv(int sock, void *buffer, size_t size)
{
    return recv(sock, buffer, size, 0);
}

inline ssize_t socket_recvfrom(int sock, void *buffer, size_t size
                               , void *sockaddr)
{
    socklen_t slen = sizeof(struct sockaddr_in);
    return recvfrom(sock, buffer, size, 0
                    , (struct sockaddr *)sockaddr
                    , &slen);
}

inline ssize_t socket_send(int sock, void *buffer, size_t size)
{
    return send(sock, buffer, size, 0);
}

inline ssize_t socket_sendto(int sock, void *buffer, size_t size
                             , void *sockaddr)
{
    return sendto(sock, buffer, size, 0
                  , (struct sockaddr *)sockaddr
                  , sizeof(struct sockaddr_in));
}

int socket_accept(int sock, char *addr, int *port)
{
    struct sockaddr_in caddr;
    memset(&caddr, 0, sizeof(caddr));
    socklen_t sin_size = sizeof(caddr);
    int csock = accept(sock, (struct sockaddr *)&caddr, &sin_size);
    if(csock <= 0)
    {
        log_error(LOG_MSG("accept() failed [%s]"), socket_error());
        return 0;
    }

    log_debug(LOG_MSG("accept() connection from %s:%d")
              , inet_ntoa(caddr.sin_addr), ntohs(caddr.sin_port));

    if(addr)
        strcpy(addr, inet_ntoa(caddr.sin_addr));
    if(port)
        *port = ntohs(caddr.sin_port);
    return csock;
}

int socket_set_buffer(int sock, int rcvbuf, int sndbuf)
{
#if defined(SO_RCVBUF) && defined(SO_SNDBUF)
    int val;
    socklen_t slen;
    if(rcvbuf > 0)
    {
        slen = sizeof(val);
#ifdef __linux__
        val = rcvbuf / 2;
#else
        val = rcvbuf;
#endif
        setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (void *)&val, slen);

        val = 0;
        getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (void *)&val, &slen);
        if(!slen || val != rcvbuf)
        {
            log_warning(LOG_MSG("failed to set receive buffer size. "
                                "current:%d required:%d")
                        , val, rcvbuf);
        }
    }
    if(sndbuf > 0)
    {
        slen = sizeof(val);
#ifdef __linux__
        val = sndbuf / 2;
#else
        val = sndbuf;
#endif
        setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (void *)&val, slen);

        val = 0;
        getsockopt(sock, SOL_SOCKET, SO_SNDBUF, (void *)&val, &slen);
        if(!slen || val != sndbuf)
        {
            log_warning(LOG_MSG("failed to set receive buffer size. "
                                "current:%d required:%d")
                        , val, sndbuf);
        }
    }
    return 1;
#else
#   warning "RCVBUF/SNDBUF is not defined"
    return 0;
#endif
}

int socket_set_timeout(int sock, int rcvmsec, int sndmsec)
{
    struct timeval tv;

    if(rcvmsec > 0)
    {
        tv.tv_sec = rcvmsec / 1000;
        tv.tv_usec = rcvmsec % 1000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (void *)&tv, sizeof(tv));
    }

    if(sndmsec > 0)
    {
        if(rcvmsec != sndmsec)
        {
            tv.tv_sec = sndmsec / 1000;
            tv.tv_usec = sndmsec % 1000;
        }
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (void *)&tv, sizeof(tv));
    }

    return 1;
}

static int _socket_multicast_cmd(int sock, int cmd, const char *addr
                                 , const char *localaddr)
{
    struct ip_mreq mreq;
    memset(&mreq, 0, sizeof(mreq));
    mreq.imr_multiaddr.s_addr = inet_addr(addr);
    if(mreq.imr_multiaddr.s_addr == INADDR_NONE)
    {
        log_error(LOG_MSG("failed to set multicast '%s'"), addr);
        return 0;
    }
    if(!IN_MULTICAST(ntohl(mreq.imr_multiaddr.s_addr)))
        return 1;

    if(localaddr)
    {
        mreq.imr_interface.s_addr = inet_addr(localaddr);
        if(mreq.imr_interface.s_addr == INADDR_NONE)
        {
            log_error(LOG_MSG("failed to set local address '%s'"), localaddr);
            mreq.imr_interface.s_addr = INADDR_ANY;
        }
    }

    if(setsockopt(sock, IPPROTO_IP, cmd
                  , (void *)&mreq, sizeof(mreq)) < 0)
    {
        log_error(LOG_MSG("failed to %s multicast [%s]")
                  , (cmd == IP_ADD_MEMBERSHIP) ? "join" : "leave"
                  , socket_error());
        return 0;
    }

    return 1;
}

int socket_multicast_join(int sock, const char *addr, const char *localaddr)
{
    return _socket_multicast_cmd(sock, IP_ADD_MEMBERSHIP, addr, localaddr);
}

int socket_multicast_leave(int sock, const char *addr, const char *localaddr)
{
    return _socket_multicast_cmd(sock, IP_DROP_MEMBERSHIP, addr, localaddr);
}

int socket_multicast_renew(int sock, const char *addr, const char *localaddr)
{
    if(!_socket_multicast_cmd(sock, IP_DROP_MEMBERSHIP, addr, localaddr))
        return 0;
    return _socket_multicast_cmd(sock, IP_ADD_MEMBERSHIP, addr, localaddr);
}

int socket_multicast_set_ttl(int sock, int ttl)
{
    if(ttl <= 0)
        return 1;
    if(setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL
                  , (void *)&ttl, sizeof(ttl)) < 0)
    {
        log_error(LOG_MSG("failed to set ttl '%d' [%s]")
                  , ttl, socket_error());
        return 0;
    }
    return 1;
}

int socket_multicast_set_if(int sock, const char *addr)
{
    if(!addr)
        return 1;

    struct in_addr laddr;
    laddr.s_addr = inet_addr(addr);
    if(setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF
                  , (void *)&laddr, sizeof(laddr)) < 0)
    {
        log_error(LOG_MSG("failed to set if '%s' [%s]")
                  , addr, socket_error());
        return 0;
    }
    return 1;
}
