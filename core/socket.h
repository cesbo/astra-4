/*
 * Astra Core
 * http://cesbo.com
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#ifndef _SOCKET_H_
#define _SOCKET_H_ 1

#include "base.h"
#include "event.h"

typedef struct asc_socket_t asc_socket_t;

typedef void (*socket_callback_t)(void * arg);

void asc_socket_core_init(void);
void asc_socket_core_destroy(void);

const char * asc_socket_error(void);

asc_socket_t * asc_socket_open_tcp4(void * arg) __wur;
asc_socket_t * asc_socket_open_udp4(void * arg) __wur;

void asc_socket_set_callback_read(asc_socket_t * sock, socket_callback_t on_read);
void asc_socket_set_callback_close(asc_socket_t * sock, socket_callback_t on_close);
void asc_socket_set_callback_send_possible(asc_socket_t * sock, socket_callback_t on_send_possible);
 
void asc_socket_shutdown_recv(asc_socket_t *sock);
void asc_socket_shutdown_send(asc_socket_t *sock);
void asc_socket_shutdown_both(asc_socket_t *sock);
void asc_socket_close(asc_socket_t *sock);

bool asc_socket_bind(asc_socket_t *sock, const char *addr, int port) __wur;
void asc_socket_listen(asc_socket_t *sock, socket_callback_t on_ok, socket_callback_t on_err);
bool asc_socket_accept(asc_socket_t *sock, asc_socket_t **client_ptr, void * arg) __wur;
void asc_socket_connect(asc_socket_t *sock, const char *addr, int port, socket_callback_t on_ok, socket_callback_t on_err);

ssize_t asc_socket_recv(asc_socket_t *sock, void *buffer, size_t size) __wur;
ssize_t asc_socket_recvfrom(asc_socket_t *sock, void *buffer, size_t size) __wur;

bool asc_socket_send_buffered(asc_socket_t *sock, const void *buffer, int size);
int  asc_socket_get_send_buffer_space_available(asc_socket_t *sock);
ssize_t asc_socket_send_direct(asc_socket_t *sock, const void *buffer, size_t size) __wur;
ssize_t asc_socket_sendto(asc_socket_t *sock, const void *buffer, size_t size) __wur;

int asc_socket_fd(asc_socket_t *sock) __wur;
const char * asc_socket_addr(asc_socket_t *sock) __wur;
int asc_socket_port(asc_socket_t *sock) __wur;

void asc_socket_set_sockaddr(asc_socket_t *sock, const char *addr, int port);
void asc_socket_set_reuseaddr(asc_socket_t *sock, int is_on);
void asc_socket_set_non_delay(asc_socket_t *sock, int is_on);
void asc_socket_set_keep_alive(asc_socket_t *sock, int is_on);
void asc_socket_set_broadcast(asc_socket_t *sock, int is_on);
void asc_socket_set_timeout(asc_socket_t *sock, int rcvmsec, int sndmsec);
void asc_socket_set_buffer(asc_socket_t *sock, int rcvbuf, int sndbuf);

void asc_socket_set_multicast_if(asc_socket_t *sock, const char *addr);
void asc_socket_set_multicast_ttl(asc_socket_t *sock, int ttl);
void asc_socket_set_multicast_loop(asc_socket_t *sock, int is_on);
void asc_socket_multicast_join(asc_socket_t *sock, const char *addr, const char *localaddr);
void asc_socket_multicast_leave(asc_socket_t *sock);
void asc_socket_multicast_renew(asc_socket_t *sock);

#endif /* _SOCKET_H_ */
