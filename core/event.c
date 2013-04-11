/*
 * Astra Core
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#include "asc.h"

#if defined(WITH_POLL)
#   define EV_TYPE_POLL
#   define MSG(_msg) "[core/event poll] " _msg
#   include <poll.h>
#   ifndef EV_STEP
#       define EV_STEP 1024
#   endif
#elif defined(WITH_SELECT) || defined(_WIN32)
#   define EV_TYPE_SELECT
#   define MSG(_msg) "[core/event select] " _msg
#   ifndef EV_STEP
#       define EV_STEP 1024
#   endif
#elif !defined(WITH_CUSTOM) && (defined(__APPLE__) || defined(__FreeBSD__))
#   define EV_TYPE_KQUEUE
#   include <sys/event.h>
#   define EV_OTYPE struct kevent
#   define EV_FLAGS (EV_EOF | EV_ERROR)
#   define EV_FFLAGS (NOTE_DELETE | NOTE_RENAME | NOTE_EXTEND)
#   ifndef EV_LIST_SIZE
#       define EV_LIST_SIZE 1024
#   endif
#   define MSG(_msg) "[core/event kqueue] " _msg
#elif !defined(WITH_CUSTOM) && defined(__linux)
#   define EV_TYPE_EPOLL
#   include <sys/epoll.h>
#   define EV_OTYPE struct epoll_event
#   if !defined(EPOLLRDHUP) && !defined(WITHOUT_EPOLLRDHUP)
#       define EV_FLAGS (EPOLLERR | EPOLLHUP)
#   else
#       define EV_FLAGS (EPOLLERR | EPOLLRDHUP)
#   endif
#   ifndef EV_LIST_SIZE
#       define EV_LIST_SIZE 1024
#   endif
#   define MSG(_msg) "[core/event epoll] " _msg
#else
#   include <windows.h>
#   include <winsock2.h>
#   define WITH_SELECT
#endif

struct asc_event_t
{
    int fd;
    void (*callback)(void *, int);
    void *arg;

#if defined(EV_TYPE_KQUEUE) || defined(EV_TYPE_SELECT)
    int is_event_read;
#endif
};

#if defined(EV_TYPE_KQUEUE) || defined(EV_TYPE_EPOLL)

/*
 * ooooooooooo oooooooooo    ooooooo  ooooo       ooooo
 *  888    88   888    888 o888   888o 888         888
 *  888ooo8     888oooo88  888     888 888         888
 *  888    oo   888        888o   o888 888      o  888      o
 * o888ooo8888 o888o         88ooo88  o888ooooo88 o888ooooo88
 *
 */

typedef struct
{
    int fd;
    EV_OTYPE ed_list[EV_LIST_SIZE];

    int fd_count;
    int detach_count;
    asc_list_t *event_list;
} event_observer_t;

static event_observer_t event_observer;

void asc_event_core_init(void)
{
    memset(&event_observer, 0, sizeof(event_observer));
    event_observer.event_list = asc_list_init();

#if defined(EV_TYPE_KQUEUE)
    event_observer.fd = kqueue();
#else
    event_observer.fd = epoll_create(1024);
#endif

    if(event_observer.fd == -1)
    {
        asc_log_error(MSG("failed to init event observer [%s]"), strerror(errno));
        abort();
    }
}

void asc_event_core_destroy(void)
{
    if(!event_observer.fd)
        return;

    asc_event_t *previous_event = NULL;
    for(asc_list_first(event_observer.event_list)
        ; asc_list_eol(event_observer.event_list)
        ; asc_list_first(event_observer.event_list))
    {
        asc_event_t *event = asc_list_data(event_observer.event_list);
        if(event == previous_event)
        {
            asc_log_error(MSG("infinite loop on observer destroing [event:%p]"), (void *)event);
            abort();
        }
        event->callback(event->arg, 0);
    }

    if(event_observer.fd > 0)
    {
        close(event_observer.fd);
        event_observer.fd = 0;
    }

    asc_list_destroy(event_observer.event_list);
}

void asc_event_core_loop(void)
{
    static struct timespec tv = { 0, 10000000 };
    if(!event_observer.fd_count)
    {
        nanosleep(&tv, NULL);
        return;
    }

#if defined(EV_TYPE_KQUEUE)
    const int ret = kevent(event_observer.fd, NULL, 0, event_observer.ed_list, EV_LIST_SIZE, &tv);
#else
    const int ret = epoll_wait(event_observer.fd, event_observer.ed_list, EV_LIST_SIZE, 10);
#endif

    if(ret == -1)
    {
        if(errno == EINTR)
            return;

        asc_log_error(MSG("event observer critical error [%s]"), strerror(errno));
        abort();
    }

    for(int i = 0; i < ret; ++i)
    {
        EV_OTYPE *ed = &event_observer.ed_list[i];
#if defined(EV_TYPE_KQUEUE)
        asc_event_t *event = ed->udata;
        const int ev_check_ok = ((ed->flags & EV_ADD)
                                 && !(ed->fflags & EV_FFLAGS) && (ed->data > 0));
        const int ev_check_err = (!ev_check_ok && (ed->flags & ~EV_ADD));
#else
        asc_event_t *event = ed->data.ptr;
        const int ev_check_ok = (!(ed->events & EPOLLERR) && ed->events & (EPOLLIN | EPOLLOUT));
        const int ev_check_err = (!ev_check_ok && (ed->events & ~(EPOLLIN | EPOLLOUT)));
#endif
        if(event->callback)
        {
            if(ev_check_ok)
                event->callback(event->arg, 1);
            if(ev_check_err)
                event->callback(event->arg, 0);
        }
    }

    if(event_observer.detach_count > 0)
    {
        asc_list_first(event_observer.event_list);
        while(asc_list_eol(event_observer.event_list))
        {
            asc_event_t *event = asc_list_data(event_observer.event_list);
            if(!event->callback)
            {
                asc_list_remove_current(event_observer.event_list);
                free(event);
                --event_observer.detach_count;
                if(!event_observer.detach_count)
                    break;
            }
            else
                asc_list_next(event_observer.event_list);
        }
        event_observer.detach_count = 0;
    }
}

static asc_event_t * __asc_event_attach(int fd
                                        , void (*callback)(void *, int), void *arg
                                        , int is_event_read)
{
#ifdef DEBUG
    asc_log_debug(MSG("attach fd=%d"), fd);
#endif

    asc_event_t *event = malloc(sizeof(asc_event_t));
    event->fd = fd;
    event->callback = callback;
    event->arg = arg;
#if defined(EV_TYPE_KQUEUE)
    event->is_event_read = is_event_read;
#endif

    int ret = -1;
    EV_OTYPE ed;

#if defined(EV_TYPE_KQUEUE)
    const int ev_filter = (is_event_read) ? EVFILT_READ : EVFILT_WRITE;
#else
    const int ev_filter = (is_event_read) ? EPOLLIN : EPOLLOUT;
#endif

    do
    {
#if defined(EV_TYPE_KQUEUE)
        EV_SET(&ed, fd, ev_filter, EV_ADD | EV_FLAGS, EV_FFLAGS, 0, event);
        ret = kevent(event_observer.fd, &ed, 1, NULL, 0, NULL);
#else
        ed.data.ptr = event;
        ed.events = ev_filter | EV_FLAGS;
        ret = epoll_ctl(event_observer.fd, EPOLL_CTL_ADD, fd, &ed);
#endif
        if(ret == -1)
        {
            asc_log_error(MSG("failed to attach fd=%d [%s]"), fd, strerror(errno));
            free(event);
            return NULL;
        }
        break;
    } while(1);

    asc_list_insert_tail(event_observer.event_list, event);
    ++event_observer.fd_count;

    return event;
}

void asc_event_close(asc_event_t *event)
{
    if(!event)
        return;

#ifdef DEBUG
    asc_log_debug(MSG("detach fd=%d"), event->fd);
#endif

    int ret;
#if defined(EV_TYPE_KQUEUE)
    int ev_filter = (event->is_event_read) ? EVFILT_READ : EVFILT_WRITE;
    EV_OTYPE ke;
    EV_SET(&ke, event->fd, ev_filter, EV_DELETE, 0, 0, event);
    ret = kevent(event_observer.fd, &ke, 1, NULL, 0, NULL);
#else
    ret = epoll_ctl(event_observer.fd, EPOLL_CTL_DEL, event->fd, NULL);
#endif

    if(ret == -1)
        asc_log_error(MSG("failed to detach fd=%d [%s]"), event->fd, strerror(errno));

    event->callback = NULL;
    ++event_observer.detach_count;

    --event_observer.fd_count;
}

#elif defined(EV_TYPE_POLL)

/*
 * oooooooooo    ooooooo  ooooo       ooooo
 *  888    888 o888   888o 888         888
 *  888oooo88  888     888 888         888
 *  888        888o   o888 888      o  888      o
 * o888o         88ooo88  o888ooooo88 o888ooooo88
 *
 */

typedef struct
{
    int fd_max;
    int fd_count;

    struct pollfd *ed_list;
    asc_event_t *event_list;
} event_observer_t;

static event_observer_t event_observer;

void asc_event_core_init(void)
{
    memset(&event_observer, 0, sizeof(event_observer));
}

void asc_event_core_destroy(void)
{
    if(!event_observer.fd_count)
        return;

    for(int i = 0; i < event_observer.fd_count; i++)
    {
        asc_event_t *event = &event_observer.event_list[i];
        if(event_observer.ed_list[i].events) // check, is callback dettach
            event->callback(event->arg, 0);
    }
    if(event_observer.fd_max > 0)
    {
        free(event_observer.ed_list);
        free(event_observer.event_list);
    }

    event_observer.fd_max = 0;
    event_observer.fd_count = 0;
}

void asc_event_core_loop(void)
{
    static struct timespec tv = { 0, 10000000 };
    if(!event_observer.fd_count)
    {
        nanosleep(&tv, NULL);
        return;
    }

    int ret = poll(event_observer.ed_list, event_observer.fd_count, 10);
    if(ret == -1)
    {
        if(errno == EINTR)
            return;

        asc_log_error(MSG("event observer critical error [%s]"), strerror(errno));
        abort();
    }

    int i;

    for(i = 0; i < event_observer.fd_count && ret > 0; ++i)
    {
        const int revents = event_observer.ed_list[i].revents;
        if(revents == 0)
            continue;
        --ret;
        asc_event_t *event = &event_observer.event_list[i];
        if(!event_observer.ed_list[i].events)
            continue;
        if(revents & (POLLIN | POLLOUT))
            event->callback(event->arg, 1);
        if(!event_observer.ed_list[i].events)
            continue;
        if(revents & (POLLERR | POLLHUP | POLLNVAL))
            event->callback(event->arg, 0);
    }

    // clean detached
    i = 0;
    while(i < event_observer.fd_count)
    {
        if(!event_observer.ed_list[i].events)
        {
            --event_observer.fd_count;
            for(int j = i; j < event_observer.fd_count; ++j)
            {
                memcpy(&event_observer.ed_list[j], &event_observer.ed_list[j+1]
                       , sizeof(struct pollfd));
                memcpy(&event_observer.event_list[j], &event_observer.event_list[j+1]
                       , sizeof(asc_event_t));
            }
        }
        else
            ++i;
    }
}

static asc_event_t * __asc_event_attach(int fd
                                        , void (*callback)(void *, int), void *arg
                                        , int is_event_read)
{
#ifdef DEBUG
    asc_log_debug(MSG("attach fd=%d"), fd);
#endif

    if(event_observer.fd_count >= event_observer.fd_max)
    {
        event_observer.fd_max += EV_STEP;
        event_observer.event_list = realloc(event_observer.event_list
                                            , sizeof(asc_event_t) * event_observer.fd_max);
        event_observer.ed_list = realloc(event_observer.ed_list
                                 , sizeof(struct pollfd) * event_observer.fd_max);
    }

    asc_event_t *event = &event_observer.event_list[event_observer.fd_count];
    event->fd = fd;
    event->callback = callback;
    event->arg = arg;

    struct pollfd *ed = &event_observer.ed_list[event_observer.fd_count];
    ed->events = (is_event_read) ? POLLIN : POLLOUT;
    ed->fd = fd;
    ed->revents = 0;

    ++event_observer.fd_count;
    return event;
}

void asc_event_close(asc_event_t *event)
{
    if(!event)
        return;

#ifdef DEBUG
    asc_log_debug(MSG("detach fd=%d"), event->fd);
#endif

    for(int i = 0; i < event_observer.fd_count; ++i)
    {
        if(event_observer.ed_list[i].fd == event->fd)
        {
            event_observer.ed_list[i].events = 0;
            return;
        }
    }

    asc_log_error(MSG("failed to detach fd=%d [not found]"), event->fd);
}

#elif defined(EV_TYPE_SELECT)

/*
 *  oooooooo8 ooooooooooo ooooo       ooooooooooo  oooooooo8 ooooooooooo
 * 888         888    88   888         888    88 o888     88 88  888  88
 *  888oooooo  888ooo8     888         888ooo8   888             888
 *         888 888    oo   888      o  888    oo 888o     oo     888
 * o88oooo888 o888ooo8888 o888ooooo88 o888ooo8888 888oooo88     o888o
 *
 */

typedef struct
{
    int max_fd;
    fd_set rmaster;
    fd_set wmaster;

    int fd_count;
    asc_list_t *event_list;
} event_observer_t;

static event_observer_t event_observer;

void asc_event_core_init(void)
{
    memset(&event_observer, 0, sizeof(event_observer));
    event_observer.event_list = asc_list_init();
}

void asc_event_core_destroy(void)
{
    if(!event_observer.fd_count)
        return;

    asc_list_for(event_observer.event_list)
    {
        asc_event_t *event = asc_list_data(event_observer.event_list);
        if(event->fd > 0)
            event->callback(event->arg, 0);
    }

    asc_list_first(event_observer.event_list);
    while(asc_list_eol(event_observer.event_list))
    {
        free(asc_list_data(event_observer.event_list));
        asc_list_remove_current(event_observer.event_list);
    }

    event_observer.fd_count = 0;
    asc_list_destroy(event_observer.event_list);
}

void asc_event_core_loop(void)
{
    if(!event_observer.fd_count)
    {
#ifdef _WIN32
        Sleep(10);
#else
        static struct timespec tv = { .tv_sec = 0, .tv_nsec = 10000000 };
        nanosleep(&tv, NULL);
#endif
        return;
    }

    fd_set rset;
    fd_set wset;
    memcpy(&rset, &event_observer.rmaster, sizeof(rset));
    memcpy(&wset, &event_observer.wmaster, sizeof(wset));

    static struct timeval timeout = { .tv_sec = 0, .tv_usec = 10000 };
    const int ret = select(event_observer.max_fd + 1, &rset, &wset, NULL, &timeout);
    if(ret == -1)
    {
        if(errno == EINTR)
            return;

        asc_log_error(MSG("event observer critical error [%s]"), strerror(errno));
        abort();
    }
    else if(ret > 0)
    {
        asc_list_for(event_observer.event_list)
        {
            asc_event_t *event = asc_list_data(event_observer.event_list);
            if(!event->fd)
                continue;
            else if(event->is_event_read)
            {
                if(FD_ISSET(event->fd, &rset))
                    event->callback(event->arg, 1);
            }
            else
            {
                if(FD_ISSET(event->fd, &wset))
                    event->callback(event->arg, 1);
            }
        }
    }

    asc_list_first(event_observer.event_list);
    while(asc_list_eol(event_observer.event_list))
    {
        asc_event_t *event = asc_list_data(event_observer.event_list);
        if(!event->fd)
        {
            asc_list_remove_current(event_observer.event_list);
            free(event);
        }
        else
            asc_list_next(event_observer.event_list);
    }
}

static asc_event_t * __asc_event_attach(int fd
                                        , void (*callback)(void *, int), void *arg
                                        , int is_event_read)
{
#ifdef DEBUG
    asc_log_debug(MSG("attach fd=%d"), fd);
#endif

    asc_event_t *event = malloc(sizeof(asc_event_t));
    event->fd = fd;
    event->callback = callback;
    event->arg = arg;
    event->is_event_read = is_event_read;

    if(is_event_read)
        FD_SET(fd, &event_observer.rmaster);
    else
        FD_SET(fd, &event_observer.wmaster);

    asc_list_insert_tail(event_observer.event_list, event);

    if(fd > event_observer.max_fd)
        event_observer.max_fd = fd;

    ++event_observer.fd_count;
    return event;
}

void asc_event_close(asc_event_t *event)
{
    if(!event)
        return;

#ifdef DEBUG
    asc_log_debug(MSG("detach fd=%d"), event->fd);
#endif

    const int fd = event->fd;
    event->fd = 0;

    if(event->is_event_read)
        FD_CLR(fd, &event_observer.rmaster);
    else
        FD_CLR(fd, &event_observer.wmaster);

    if(event_observer.max_fd == fd)
    {
        event_observer.max_fd = 0;
        asc_list_for(event_observer.event_list)
        {
            event = asc_list_data(event_observer.event_list);
            if(event->fd > event_observer.max_fd)
                event_observer.max_fd = event->fd;
        }
    }

    --event_observer.fd_count;
}

#endif

asc_event_t * asc_event_on_read(int fd, void (*callback)(void *, int), void *arg)
{
    return __asc_event_attach(fd, callback, arg, 1);
}

asc_event_t * asc_event_on_write(int fd, void (*callback)(void *, int), void *arg)
{
    return __asc_event_attach(fd, callback, arg, 0);
}
