/*
 * Astra Core
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
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
#   include <fcntl.h>
#endif

struct asc_stream_t
{
    int gate[2];

    asc_event_t *event;

    void (*callback)(void *);
    void *arg;
};

#ifdef WIN32
/* Copyright 2007 by Nathan C. Myers <ncm@cantrip.org> */
static int asc_stream_open(int socks[2])
{
    union
    {
        struct sockaddr_in inaddr;
        struct sockaddr addr;
    } a;

    int listener;
    int e;
    socklen_t addrlen = sizeof(a.inaddr);
    // DWORD flags = (make_overlapped ? WSA_FLAG_OVERLAPPED : 0);
    DWORD flags = WSA_FLAG_OVERLAPPED;
    int reuse = 1;

    if (socks == 0) {
        WSASetLastError(WSAEINVAL);
        return 0;
    }

    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET)
        return 0;

    memset(&a, 0, sizeof(a));
    a.inaddr.sin_family = AF_INET;
    a.inaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.inaddr.sin_port = 0;

    socks[0] = socks[1] = INVALID_SOCKET;
    do
    {
        if(setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
                       (char*) &reuse, (socklen_t) sizeof(reuse)) == -1)
        {
            break;
        }
        if(bind(listener, &a.addr, sizeof(a.inaddr)) == SOCKET_ERROR)
            break;
        if(getsockname(listener, &a.addr, &addrlen) == SOCKET_ERROR)
            break;
        if(listen(listener, 1) == SOCKET_ERROR)
            break;
        socks[0] = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, flags);
        if(socks[0] == INVALID_SOCKET)
            break;
        if(connect(socks[0], &a.addr, sizeof(a.inaddr)) == SOCKET_ERROR)
            break;
        socks[1] = accept(listener, NULL, NULL);
        if(socks[1] == INVALID_SOCKET)
            break;

        closesocket(listener);

        // nonblock
        unsigned long nonblock = 1;
        ioctlsocket(socks[0], FIONBIO, &nonblock);
        ioctlsocket(socks[1], FIONBIO, &nonblock);

        return 1;

    } while(0);

    e = WSAGetLastError();
    closesocket(listener);
    closesocket(socks[0]);
    closesocket(socks[1]);
    WSASetLastError(e);
    return 0;
}
#else
static int asc_stream_open(int socks[2])
{
    if(socketpair(AF_LOCAL, SOCK_STREAM, 0, socks) == 0)
    {
        // nonblock
        int flags = fcntl(socks[0], F_GETFL) | O_NONBLOCK;
        fcntl(socks[0], F_SETFL, flags);
        fcntl(socks[1], F_SETFL, flags);
        return 1;
    }
    return 0;
}
#endif

static void asc_stream_close(int socks[2])
{
    if(socks[0] > 0)
    {
#ifdef _WIN32
        closesocket(socks[0]);
#else
        close(socks[0]);
#endif
        socks[0] = 0;
    }
    if(socks[1] > 0)
    {
#ifdef _WIN32
        closesocket(socks[1]);
#else
        close(socks[1]);
#endif
        socks[1] = 0;
    }
}

static void asc_stream_event(void *arg, int is_data)
{
    if(!is_data)
        return;

    asc_stream_t *s = (asc_stream_t *)arg;
    s->callback(s->arg);
}

asc_stream_t * asc_stream_init(void (*callback)(void *), void *arg)
{
    asc_stream_t *s = (asc_stream_t *)calloc(1, sizeof(asc_stream_t));

    if(!asc_stream_open(s->gate))
    {
        free(s);
        return NULL;
    }
    s->callback = callback;
    s->arg = arg;
    s->event = asc_event_on_read(s->gate[1], asc_stream_event, s);

    return s;
}

void asc_stream_destroy(asc_stream_t *s)
{
    if(!s)
        return;
    asc_event_close(s->event);
    asc_stream_close(s->gate);
    free(s);
}

ssize_t asc_stream_send(asc_stream_t *s, void *data, size_t size)
{
    return send(s->gate[0], data, size, 0);
}

ssize_t asc_stream_recv(asc_stream_t *s, void *data, size_t size)
{
    return recv(s->gate[1], data, size, 0);
}
