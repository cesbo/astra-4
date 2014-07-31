/*
 * Astra Core
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

#ifndef _SOCKET_H_
#define _SOCKET_H_ 1

#include "base.h"
#include "event.h"

typedef struct asc_socket_t asc_socket_t;

void asc_socket_core_init(void);
void asc_socket_core_destroy(void);

const char * asc_socket_error(void);

asc_socket_t * asc_socket_open_tcp4(void * arg) __wur;
asc_socket_t * asc_socket_open_udp4(void * arg) __wur;
asc_socket_t * asc_socket_open_sctp4(void * arg) __wur;

void asc_socket_set_on_read(asc_socket_t * sock, event_callback_t on_read);
void asc_socket_set_on_close(asc_socket_t * sock, event_callback_t on_close);
void asc_socket_set_on_ready(asc_socket_t * sock, event_callback_t on_ready);

void asc_socket_shutdown_recv(asc_socket_t *sock);
void asc_socket_shutdown_send(asc_socket_t *sock);
void asc_socket_shutdown_both(asc_socket_t *sock);
void asc_socket_close(asc_socket_t *sock);

bool asc_socket_bind(asc_socket_t *sock, const char *addr, int port) __wur;
void asc_socket_listen(  asc_socket_t *sock
                       , event_callback_t on_accept, event_callback_t on_error);
bool asc_socket_accept(asc_socket_t *sock, asc_socket_t **client_ptr, void *arg) __wur;
void asc_socket_connect(  asc_socket_t *sock, const char *addr, int port
                        , event_callback_t on_connect, event_callback_t on_error);

ssize_t asc_socket_recv(asc_socket_t *sock, void *buffer, size_t size) __wur;
ssize_t asc_socket_recvfrom(asc_socket_t *sock, void *buffer, size_t size) __wur;

ssize_t asc_socket_send(asc_socket_t *sock, const void *buffer, size_t size) __wur;
ssize_t asc_socket_sendto(asc_socket_t *sock, const void *buffer, size_t size) __wur;

int asc_socket_fd(asc_socket_t *sock) __wur;
const char * asc_socket_addr(asc_socket_t *sock) __wur;
int asc_socket_port(asc_socket_t *sock) __wur;

void asc_socket_set_nonblock(asc_socket_t *sock, bool is_nonblock);
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
