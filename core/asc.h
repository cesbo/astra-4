/*
 * Astra Core
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#ifndef _ASC_H_
#define _ASC_H_ 1

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define ARRAY_SIZE(_a) (sizeof(_a)/sizeof(_a[0]))
#define UNUSED_ARG(_x) (void)_x

/* event.c */

typedef struct event_s event_t;

typedef enum
{
    EVENT_NONE      = 0x00,
    EVENT_READ      = 0x01,
    EVENT_WRITE     = 0x02,
    EVENT_ERROR     = 0xF0
} event_type_t;

void event_observer_init(void);
void event_observer_loop(void);
void event_observer_destroy(void);

event_t * event_attach(int fd, event_type_t type, void (*callback)(void *, int), void *arg);
void event_detach(event_t *event);

/* timer.c */

void timer_action(void);
void timer_destroy(void);

void timer_one_shot(unsigned int, void (*)(void *), void *);

void * timer_attach(unsigned int, void (*)(void *), void *);
void timer_detach(void *);

/* list.c */

typedef struct list_s list_t;

list_t * list_init(void);
void list_destroy(list_t *list);

void list_first(list_t *list);
void list_next(list_t *list);
int list_is_data(list_t *list);
void * list_data(list_t *list);

void list_insert_head(list_t *list, void *data);
void list_insert_tail(list_t *list, void *data);

void list_remove_current(list_t *list);
void list_remove_item(list_t *list, void *data);

#define list_for(__list) for(list_first(__list); list_is_data(__list); list_next(__list))

/* log.c */

void log_set_stdout(int);
void log_set_debug(int);
void log_set_file(const char *);
#ifndef _WIN32
void log_set_syslog(const char *);
#endif

void log_hup(void);
void log_destroy(void);

void log_info(const char *, ...);
void log_error(const char *, ...);
void log_warning(const char *, ...);
void log_debug(const char *, ...);

/* socket.c */

typedef struct socket_s socket_t;

void socket_init(void);
void socket_destroy(void);

char * socket_error(void);

socket_t * socket_open_tcp4(void);
socket_t * socket_open_udp4(void);

void socket_shutdown_recv(socket_t *sock);
void socket_shutdown_send(socket_t *sock);
void socket_shutdown_both(socket_t *sock);
void socket_close(socket_t *sock);

int socket_bind(socket_t *sock, const char *addr, int port);
int socket_accept(socket_t *sock, socket_t *client);
int socket_connect(socket_t *sock, const char *addr, int port);

ssize_t socket_recv(socket_t *sock, void *buffer, size_t size);
ssize_t socket_recvfrom(socket_t *sock, void *buffer, size_t size);

ssize_t socket_send(socket_t *sock, const void *buffer, size_t size);
ssize_t socket_sendto(socket_t *sock, const void *buffer, size_t size);

int socket_fd(socket_t *sock);
const char * socket_addr(socket_t *sock);
int socket_port(socket_t *sock);

void socket_set_sockaddr(socket_t *sock, const char *addr, int port);
void socket_set_nonblock(socket_t *sock, int is_nonblock);
void socket_set_reuseaddr(socket_t *sock, int is_on);
void socket_set_non_delay(socket_t *sock, int is_on);
void socket_set_keep_alive(socket_t *sock, int is_on);
void socket_set_broadcast(socket_t *sock, int is_on);
void socket_set_timeout(socket_t *sock, int rcvmsec, int sndmsec);
void socket_set_buffer(socket_t *sock, int rcvbuf, int sndbuf);

void socket_set_multicast_if(socket_t *sock, const char *addr);
void socket_set_multicast_ttl(socket_t *sock, int ttl);
void socket_set_multicast_loop(socket_t *sock, int is_on);
void socket_multicast_join(socket_t *sock, const char *addr, int port, const char *localaddr);
void socket_multicast_leave(socket_t *sock);
void socket_multicast_renew(socket_t *sock);

/* stream.c */

typedef struct stream_s stream_t;

stream_t * stream_init(void (*)(void *), void *);
void stream_destroy(stream_t *);

ssize_t stream_send(stream_t *, void *, size_t);
ssize_t stream_recv(stream_t *, void *, size_t);

/* thread.c */

typedef struct thread_s thread_t;

int thread_init(thread_t **, void (*)(void *), void *);
void thread_destroy(thread_t **);

int thread_is_started(thread_t *);

/* */

#define ASC_INIT()                                                          \
    socket_init();                                                          \
    event_init();

#define ASC_LOOP()                                                          \
    while(1)                                                                \
    {                                                                       \
        event_action();                                                     \
        timer_action();                                                     \
    }

#define ASC_DESTROY()                                                       \
    timer_destroy();                                                        \
    event_destroy();                                                        \
    socket_destroy();                                                       \
    log_info("[main] exit");                                                \
    log_destroy();

#endif /* _ASC_H_ */
