/*
 * AsC Framework
 * http://cesbo.com
 *
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
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

#ifdef _WIN32
#   if defined(ASC)
#       define ASC_API __declspec(dllexport)
#   else
#       define ASC_API
#   endif
#else
#   define ASC_API extern
#endif

#define ARRAY_SIZE(_a) (sizeof(_a)/sizeof(_a[0]))
#define UNUSED_ARG(_x) (void)_x

/* event.c */

enum
{
    EVENT_NONE      = 0x00,
    EVENT_READ      = 0x01,
    EVENT_WRITE     = 0x02,
    EVENT_ERROR     = 0xF0
};

ASC_API int event_init(void);
ASC_API void event_action(void);
ASC_API void event_destroy(void);

ASC_API int event_attach(int, void (*)(void *, int), void *, int);
ASC_API void event_detach(int);

/* timer.c */

ASC_API void timer_action(void);
ASC_API void timer_destroy(void);

ASC_API void timer_one_shot(unsigned int, void (*)(void *), void *);

ASC_API void * timer_attach(unsigned int, void (*)(void *), void *);
ASC_API void timer_detach(void *);

/* list.c */

typedef struct list_s list_t;

ASC_API list_t * list_append(list_t *, void *);
ASC_API list_t * list_insert(list_t *, void *);
ASC_API list_t * list_delete(list_t *, void *);
ASC_API list_t * list_get_first(list_t *);
ASC_API list_t * list_get_next(list_t *);
ASC_API void * list_get_data(list_t *);

/* log.c */

ASC_API void log_set_stdout(int);
ASC_API void log_set_debug(int);
ASC_API void log_set_file(const char *);
#ifndef _WIN32
ASC_API void log_set_syslog(const char *);
#endif

ASC_API void log_hup(void);
ASC_API void log_destroy(void);

ASC_API void log_info(const char *, ...);
ASC_API void log_error(const char *, ...);
ASC_API void log_warning(const char *, ...);
ASC_API void log_debug(const char *, ...);

/* socket.c */

enum
{
    SOCKET_FAMILY_IPv4      = 0x00000001,
    SOCKET_FAMILY_IPv6      = 0x00000002,
    SOCKET_PROTO_TCP        = 0x00000004,
    SOCKET_PROTO_UDP        = 0x00000008,
    SOCKET_REUSEADDR        = 0x00000010,
    SOCKET_BLOCK            = 0x00000020,
    SOCKET_BROADCAST        = 0x00000040,
    SOCKET_LOOP_DISABLE     = 0x00000080,
    SOCKET_BIND             = 0x00000100,
    SOCKET_CONNECT          = 0x00000200,
    SOCKET_NO_DELAY         = 0x00000400,
    SOCKET_KEEP_ALIVE       = 0x00000800
};

enum
{
    SOCKET_SHUTDOWN_RECV    = 1,
    SOCKET_SHUTDOWN_SEND    = 2,
    SOCKET_SHUTDOWN_BOTH    = 3
};

ASC_API void socket_init(void);
ASC_API void socket_destroy(void);

ASC_API int socket_open(int, const char *, int);
ASC_API int socket_shutdown(int, int);
ASC_API void socket_close(int);
ASC_API char * socket_error(void);

ASC_API int socket_options_set(int, int);
ASC_API int socket_port(int);

ASC_API int socket_accept(int, char *, int *);

ASC_API int socket_set_buffer(int, int, int);
ASC_API int socket_set_timeout(int, int, int);

ASC_API int socket_multicast_join(int, const char *, const char *);
ASC_API int socket_multicast_leave(int, const char *, const char *);
ASC_API int socket_multicast_renew(int, const char *, const char *);
ASC_API int socket_multicast_set_ttl(int, int);
ASC_API int socket_multicast_set_if(int, const char *);

ASC_API ssize_t socket_recv(int, void *, size_t);
ASC_API ssize_t socket_send(int, void *, size_t);

ASC_API void * socket_sockaddr_init(const char *, int);
ASC_API void socket_sockaddr_destroy(void *sockaddr);

ASC_API ssize_t socket_recvfrom(int, void *, size_t, void *);
ASC_API ssize_t socket_sendto(int, void *, size_t, void *);

/* stream.c */

typedef struct stream_s stream_t;

ASC_API stream_t * stream_init(void (*)(void *), void *);
ASC_API void stream_destroy(stream_t *);

ASC_API ssize_t stream_send(stream_t *, void *, size_t);
ASC_API ssize_t stream_recv(stream_t *, void *, size_t);

/* thread.c */

typedef struct thread_s thread_t;

ASC_API int thread_init(thread_t **, void (*)(void *), void *);
ASC_API void thread_destroy(thread_t **);

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
