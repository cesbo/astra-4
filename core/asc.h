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
#include <setjmp.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define ASC_ARRAY_SIZE(_a) (sizeof(_a)/sizeof(_a[0]))
#define __uarg(_x) {(void)_x;}

#ifndef __wur
#   define __wur __attribute__(( __warn_unused_result__ ))
#endif

/* event.c */

typedef struct asc_event_t asc_event_t;

void asc_event_core_init(void);
void asc_event_core_loop(void);
void asc_event_core_destroy(void);

asc_event_t * asc_event_on_read(int fd, void (*callback)(void *, int), void *arg) __wur;
asc_event_t * asc_event_on_write(int fd, void (*callback)(void *, int), void *arg) __wur;
void asc_event_close(asc_event_t *event);

/* timer.c */

typedef struct asc_timer_t asc_timer_t;

void asc_timer_core_init(void);
void asc_timer_core_loop(void);
void asc_timer_core_destroy(void);

void asc_timer_one_shot(unsigned int ms, void (*callback)(void *), void *arg);

asc_timer_t * asc_timer_init(unsigned int ms, void (*callback)(void *), void *arg) __wur;
void asc_timer_destroy(asc_timer_t *timer);

/* list.c */

typedef struct asc_list_t asc_list_t;

asc_list_t * asc_list_init(void) __wur;
void asc_list_destroy(asc_list_t *list);

void asc_list_first(asc_list_t *list);
void asc_list_next(asc_list_t *list);
int asc_list_eol(asc_list_t *list) __wur;
void * asc_list_data(asc_list_t *list) __wur;

void asc_list_insert_head(asc_list_t *list, void *data);
void asc_list_insert_tail(asc_list_t *list, void *data);

void asc_list_remove_current(asc_list_t *list);
void asc_list_remove_item(asc_list_t *list, void *data);

#define asc_list_for(__list) \
    for(asc_list_first(__list); asc_list_eol(__list); asc_list_next(__list))

/* log.c */

void asc_log_set_stdout(int);
void asc_log_set_debug(int);
void asc_log_set_file(const char *);
#ifndef _WIN32
void asc_log_set_syslog(const char *);
#endif

void asc_log_hup(void);
void asc_log_core_destroy(void);

void asc_log_info(const char *, ...);
void asc_log_error(const char *, ...);
void asc_log_warning(const char *, ...);
void asc_log_debug(const char *, ...);

/* socket.c */

typedef struct asc_socket_t asc_socket_t;

void asc_socket_core_init(void);
void asc_socket_core_destroy(void);

const char * asc_socket_error(void);

asc_socket_t * asc_socket_open_tcp4(void) __wur;
asc_socket_t * asc_socket_open_udp4(void) __wur;

void asc_socket_shutdown_recv(asc_socket_t *sock);
void asc_socket_shutdown_send(asc_socket_t *sock);
void asc_socket_shutdown_both(asc_socket_t *sock);
void asc_socket_close(asc_socket_t *sock);

int asc_socket_bind(asc_socket_t *sock, const char *addr, int port) __wur;
int asc_socket_accept(asc_socket_t *sock, asc_socket_t **client_ptr) __wur;
int asc_socket_connect(asc_socket_t *sock, const char *addr, int port) __wur;

ssize_t asc_socket_recv(asc_socket_t *sock, void *buffer, size_t size) __wur;
ssize_t asc_socket_recvfrom(asc_socket_t *sock, void *buffer, size_t size) __wur;

ssize_t asc_socket_send(asc_socket_t *sock, const void *buffer, size_t size) __wur;
ssize_t asc_socket_sendto(asc_socket_t *sock, const void *buffer, size_t size) __wur;

int asc_socket_fd(asc_socket_t *sock) __wur;
const char * asc_socket_addr(asc_socket_t *sock) __wur;
int asc_socket_port(asc_socket_t *sock) __wur;

int asc_socket_event_on_accept(asc_socket_t *sock, void (*callback)(void *, int), void *arg);
int asc_socket_event_on_read(asc_socket_t *sock, void (*callback)(void *, int), void *arg);
int asc_socket_event_on_connect(asc_socket_t *sock, void (*callback)(void *, int), void *arg);

void asc_socket_set_sockaddr(asc_socket_t *sock, const char *addr, int port);
void asc_socket_set_nonblock(asc_socket_t *sock, int is_nonblock);
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

/* thread.c */

typedef struct asc_thread_t asc_thread_t;

#ifndef _WIN32
jmp_buf * __thread_getjmp(void);
void __thread_setjmp(asc_thread_t *thread);
#   define asc_thread_while(_thread)                                                            \
        const int __thread_loop = setjmp(*__thread_getjmp());                                   \
        if(!__thread_loop) __thread_setjmp(_thread);                                            \
        while(!__thread_loop)
#else
#   define thread_while() while(1)
#endif

void asc_thread_init(asc_thread_t **thread_ptr, void (*loop)(void *), void *arg);
void asc_thread_destroy(asc_thread_t **thread_ptr);

/* */

#define ASC_INIT()                                                                              \
    asc_timer_core_init();                                                                      \
    asc_socket_core_init();                                                                     \
    asc_event_core_init();

#define ASC_LOOP()                                                                              \
    while(1)                                                                                    \
    {                                                                                           \
        asc_event_core_loop();                                                                  \
        asc_timer_core_loop();                                                                  \
    }

#define ASC_DESTROY()                                                                           \
    asc_event_core_destroy();                                                                   \
    asc_socket_core_destroy();                                                                  \
    asc_timer_core_destroy();                                                                   \
    asc_log_info("[main] exit");                                                                \
    asc_log_core_destroy();

#endif /* _ASC_H_ */
