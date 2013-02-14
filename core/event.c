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

struct event_s
{
    int fd;
    void (*callback)(void *, int);
    void *arg;
    int type;
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
    list_t *event_list;
} event_observer_t;

static event_observer_t event_observer;

void event_observer_init(void)
{
    memset(&event_observer, 0, sizeof(event_observer));
    event_observer.event_list = list_init();

#if defined(EV_TYPE_KQUEUE)
    event_observer.fd = kqueue();
#else
    event_observer.fd = epoll_create(1024);
#endif

    if(event_observer.fd == -1)
    {
        log_error(MSG("failed to init event observer [%s]"), strerror(errno));
        abort();
    }
}

void event_observer_destroy(void)
{
    if(!event_observer.fd)
        return;

    event_t *previous_event = NULL;
    for(list_first(event_observer.event_list)
        ; list_is_data(event_observer.event_list)
        ; list_first(event_observer.event_list))
    {
        event_t *event = (event_t *)list_data(event_observer.event_list);
        if(event == previous_event)
        {
            log_error(MSG("infinite loop on observer destroing [event:%p]"), (void *)event);
            abort();
        }
        event->callback(event->arg, EVENT_ERROR);
    }

    if(event_observer.fd > 0)
    {
        close(event_observer.fd);
        event_observer.fd = 0;
    }

    list_destroy(event_observer.event_list);
}

void event_observer_loop(void)
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

        log_warning(MSG("event observer critical error [%s]"), strerror(errno));
        abort();
    }

    for(int i = 0; i < ret; ++i)
    {
        EV_OTYPE *ed = &event_observer.ed_list[i];
#if defined(EV_TYPE_KQUEUE)
        event_t *event = (event_t *)ed->udata;
        const int ev_check_ok = ((ed->flags & EV_ADD)
                                 && !(ed->fflags & EV_FFLAGS) && (ed->data > 0));
        const int ev_check_err = (!ev_check_ok && (ed->flags & ~EV_ADD));
#else
        event_t *event = (event_t *)ed->data.ptr;
        const int ev_check_ok = (!(ed->events & EPOLLERR) && ed->events & (EPOLLIN | EPOLLOUT));
        const int ev_check_err = (!ev_check_ok && (ed->events & ~(EPOLLIN | EPOLLOUT)));
#endif
        if(ev_check_ok)
            event->callback(event->arg, event->type);
        if(ev_check_err)
            event->callback(event->arg, EVENT_ERROR);
    }
}

event_t * event_attach(int fd, event_type_t type, void (*callback)(void *, int), void *arg)
{
#ifdef DEBUG
    log_debug(MSG("attach fd=%d"), fd);
#endif

    event_t *event = (event_t *)malloc(sizeof(event_t));
    event->fd = fd;
    event->callback = callback;
    event->arg = arg;
    event->type = type;

    int ret = -1;
    EV_OTYPE ed;

#if defined(EV_TYPE_KQUEUE)
    const int ev_filter = (type == EVENT_READ) ? EVFILT_READ : EVFILT_WRITE;
#else
    const int ev_filter = (type == EVENT_READ) ? EPOLLIN : EPOLLOUT;
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
            log_error(MSG("failed to attach fd=%d [%s]"), fd, strerror(errno));
            free(event);
            return NULL;
        }
        break;
    } while(1);

    list_insert_tail(event_observer.event_list, event);
    ++event_observer.fd_count;

    return event;
}

void event_detach(event_t *event)
{
    if(!event)
        return;

#ifdef DEBUG
    log_debug(MSG("detach fd=%d"), event->fd);
#endif

    int ret;
#if defined(EV_TYPE_KQUEUE)
    int ev_filter = (EVENT_READ == event->type) ? EVFILT_READ : EVFILT_WRITE;
    EV_OTYPE ke;
    EV_SET(&ke, event->fd, ev_filter, EV_DELETE, 0, 0, event);
    ret = kevent(event_observer.fd, &ke, 1, NULL, 0, NULL);
#else
    ret = epoll_ctl(event_observer.fd, EPOLL_CTL_DEL, event->fd, NULL);
#endif

    if(ret == -1)
        log_error(MSG("failed to detach fd=%d [%s]"), event->fd, strerror(errno));

    list_remove_item(event_observer.event_list, event);
    free(event);
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
    event_t *event_list;
} event_observer_t;

static event_observer_t event_observer;

void event_observer_init(void)
{
    memset(&event_observer, 0, sizeof(event_observer));
}

void event_observer_destroy(void)
{
    if(!event_observer.fd_count)
        return;

    for(int i = 0; i < event_observer.fd_count; i++)
    {
        event_t *event = &event_observer.event_list[i];
        if(event_observer.ed_list[i].events) // check, is callback dettach
            event->callback(event->arg, EVENT_ERROR);
    }
    if(event_observer.fd_max > 0)
    {
        free(event_observer.ed_list);
        free(event_observer.event_list);
    }

    event_observer.fd_max = 0;
    event_observer.fd_count = 0;
}

void event_observer_loop(void)
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

        log_warning(MSG("event observer critical error [%s]"), strerror(errno));
        abort();
    }

    int i;

    for(i = 0; i < event_observer.fd_count && ret > 0; ++i)
    {
        const int revents = event_observer.ed_list[i].revents;
        if(revents == 0)
            continue;
        --ret;
        event_t *event = &event_observer.event_list[i];
        if(!event_observer.ed_list[i].events)
            continue;
        if(revents & (POLLIN | POLLOUT))
            event->callback(event->arg, event->type);
        if(!event_observer.ed_list[i].events)
            continue;
        if(revents & (POLLERR | POLLHUP | POLLNVAL))
            event->callback(event->arg, EVENT_ERROR);
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
                       , sizeof(event_t));
            }
        }
        else
            ++i;
    }
}

event_t * event_attach(int fd, event_type_t type, void (*callback)(void *, int), void *arg)
{
#ifdef DEBUG
    log_debug(MSG("attach fd=%d"), fd);
#endif

    if(event_observer.fd_count >= event_observer.fd_max)
    {
        event_observer.fd_max += EV_STEP;
        event_observer.event_list = realloc(event_observer.event_list
                                            , sizeof(event_t) * event_observer.fd_max);
        event_observer.ed_list = realloc(event_observer.ed_list
                                 , sizeof(struct pollfd) * event_observer.fd_max);
    }

    event_t *event = &event_observer.event_list[event_observer.fd_count];
    event->fd = fd;
    event->callback = callback;
    event->arg = arg;
    event->type = type;

    struct pollfd *ed = &event_observer.ed_list[event_observer.fd_count];
    ed->events = (type == EVENT_READ) ? POLLIN : POLLOUT;
    ed->fd = fd;
    ed->revents = 0;

    ++event_observer.fd_count;
    return event;
}

void event_detach(event_t *event)
{
    if(!event)
        return;

#ifdef DEBUG
    log_debug(MSG("detach fd=%d"), event->fd);
#endif

    for(int i = 0; i < event_observer.fd_count; ++i)
    {
        if(event_observer.ed_list[i].fd == event->fd)
        {
            event_observer.ed_list[i].events = 0;
            return;
        }
    }

    log_error(MSG("failed to detach fd=%d [not found]"), event->fd);
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
    list_t *event_list;
} event_observer_t;

static event_observer_t event_observer;

void event_observer_init(void)
{
    memset(&event_observer, 0, sizeof(event_observer));
    event_observer.event_list = list_init();
}

void event_observer_destroy(void)
{
    if(!event_observer.fd_count)
        return;

    list_for(event_observer.event_list)
    {
        event_t *event = (event_t *)list_data(event_observer.event_list);
        if(event->fd > 0)
            event->callback(event->arg, EVENT_ERROR);
    }

    list_first(event_observer.event_list);
    while(list_is_data(event_observer.event_list))
    {
        free(list_data(event_observer.event_list));
        list_remove_current(event_observer.event_list);
    }

    event_observer.fd_count = 0;
    list_destroy(event_observer.event_list);
}

void event_observer_loop(void)
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
    int ret = select(event_observer.max_fd + 1, &rset, &wset, NULL, &timeout);
    if(ret == -1)
    {
        if(errno == EINTR)
            return;

        log_warning(MSG("event observer critical error [%s]"), strerror(errno));
        abort();
    }

    list_for(event_observer.event_list)
    {
        event_t *event = (event_t *)list_data(event_observer.event_list);
        if(!event->fd)
            continue;
        else if(event->type == EVENT_READ)
        {
            if(FD_ISSET(event->fd, &rset))
                event->callback(event->arg, EVENT_READ);
        }
        else if(event->type == EVENT_WRITE)
        {
            if(FD_ISSET(event->fd, &wset))
                event->callback(event->arg, EVENT_WRITE);
        }
    }

    list_first(event_observer.event_list);
    while(list_is_data(event_observer.event_list))
    {
        event_t *event = (event_t *)list_data(event_observer.event_list);
        if(!event->fd)
        {
            list_remove_current(event_observer.event_list);
            free(event);
        }
        else
            list_next(event_observer.event_list);
    }
}

event_t * event_attach(int fd, event_type_t type, void (*callback)(void *, int), void *arg)
{
#ifdef DEBUG
    log_debug(MSG("attach fd=%d"), fd);
#endif

    event_t *event = malloc(sizeof(event_t));
    event->fd = fd;
    event->callback = callback;
    event->arg = arg;
    event->type = type;

    if(type == EVENT_READ)
        FD_SET(fd, &event_observer.rmaster);
    else if(type == EVENT_WRITE)
        FD_SET(fd, &event_observer.wmaster);

    list_insert_tail(event_observer.event_list, event);

    if(fd > event_observer.max_fd)
        event_observer.max_fd = fd;

    ++event_observer.fd_count;
    return event;
}

void event_detach(event_t *event)
{
    if(!event)
        return;

#ifdef DEBUG
    log_debug(MSG("detach fd=%d"), event->fd);
#endif

    const int fd = event->fd;
    event->fd = 0;

    if(event->type == EVENT_READ)
        FD_CLR(fd, &event_observer.rmaster);
    else if(event->type == EVENT_WRITE)
        FD_CLR(fd, &event_observer.wmaster);

    if(event_observer.max_fd == fd)
    {
        event_observer.max_fd = 0;
        list_for(event_observer.event_list)
        {
            event = (event_t *)list_data(event_observer.event_list);
            if(event->fd > event_observer.max_fd)
                event_observer.max_fd = event->fd;
        }
    }

    --event_observer.fd_count;
}

#endif
