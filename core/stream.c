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
#endif

struct stream_s
{
    int gate[2];

    void (*callback)(void *);
    void *arg;
};

#ifdef WIN32
/* Copyright 2007 by Nathan C. Myers <ncm@cantrip.org> */
static int stream_gate_open(int socks[2])
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
        socket_options_set(socks[0], 0);
        socket_options_set(socks[1], 0);

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
static int stream_gate_open(int socks[2])
{
    if(socketpair(AF_LOCAL, SOCK_STREAM, 0, socks) == 0)
    {
        // nonblock
        socket_options_set(socks[0], 0);
        socket_options_set(socks[1], 0);
        return 1;
    }
    return 0;
}
#endif

static void stream_gate_close(int socks[2])
{
    if(socks[0] > 0)
    {
        socket_close(socks[0]);
        socks[0] = 0;
    }
    if(socks[1] > 0)
    {
        socket_close(socks[1]);
        socks[1] = 0;
    }
}

static void stream_event(void *arg, int event)
{
    if(event == EVENT_ERROR)
    {
        return;
    }

    stream_t *s = (stream_t *)arg;
    s->callback(s->arg);
}

stream_t * stream_init(void (*callback)(void *), void *arg)
{
    stream_t *s = (stream_t *)calloc(1, sizeof(stream_t));

    if(!stream_gate_open(s->gate))
    {
        free(s);
        return NULL;
    }
    s->callback = callback;
    s->arg = arg;
    event_attach(s->gate[1], stream_event, s, EVENT_READ);

    return s;
}

void stream_destroy(stream_t *s)
{
    if(!s)
        return;
    event_detach(s->gate[1]);
    stream_gate_close(s->gate);
    free(s);
}

ssize_t stream_send(stream_t *s, void *data, size_t size)
{
    return socket_send(s->gate[0], data, size);
}

ssize_t stream_recv(stream_t *s, void *data, size_t size)
{
    return socket_recv(s->gate[1], data, size);
}
