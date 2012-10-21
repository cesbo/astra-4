/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#define ASTRA_CORE
#include <astra.h>

#ifndef EVENT_STEP
#   define EVENT_STEP 32
#endif

#if defined(WITH_POLL) || defined(WITH_SELECT)
#   define WITH_CUSTOM 1
#endif

#if !defined(WITH_CUSTOM) && (defined(__APPLE__) || defined(__FreeBSD__))
#   define EV_TYPE_KQUEUE
#   include <sys/event.h>
#   define EV_ETYPE struct kevent
#   define EV_FLAGS (EV_EOF | EV_ERROR)
#   define EV_FFLAGS (NOTE_DELETE | NOTE_RENAME | NOTE_EXTEND)
#   define LOG_MSG(_msg) "[core/event kqueue] " _msg
#elif !defined(WITH_CUSTOM) && defined(__linux)
#   define EV_TYPE_EPOLL
#   include <sys/epoll.h>
#   define EV_ETYPE struct epoll_event
#   if !defined(EPOLLRDHUP) && !defined(WITHOUT_EPOLLRDHUP)
#       define WITHOUT_EPOLLRDHUP 1
#   endif
#   if defined(WITHOUT_EPOLLRDHUP)
#       define EV_FLAGS (EPOLLERR | EPOLLHUP)
#   else
#       define EV_FLAGS (EPOLLERR | EPOLLRDHUP)
#   endif
#   define LOG_MSG(_msg) "[core/event epoll] " _msg
#elif defined(_WIN32)
#   include <windows.h>
#   include <winsock2.h>
#   ifdef WITH_POLL
#       undef WITH_POLL
#   endif
#   ifndef WITH_SELECT
#       define WITH_SELECT
#   endif
#endif

#if defined(WITH_POLL)
#   define EV_TYPE_POLL
#   define LOG_MSG(_msg) "[core/event poll] " _msg
#   include <poll.h>
#elif defined(WITH_SELECT)
#   define EV_TYPE_SELECT
#   define LOG_MSG(_msg) "[core/event select] " _msg
#endif

typedef struct event_s event_t;
static event_t *event = NULL;

typedef struct
{
    int fd;
    void (*cb)(void *, int);
    void *arg;
    int type;
} event_item_t;

#if defined(EV_TYPE_KQUEUE) || defined(EV_TYPE_EPOLL)

struct event_s
{
    int fd_max;
    int fd_count;

    int efd;
    EV_ETYPE *ed_list;
    list_t *ev_list;
};

/* kqueue/epoll */
void event_detach(int fd)
{
    if(fd <= 0)
        return;

    int ret;
    event_item_t *ev;
    list_t *item = list_get_first(event->ev_list);
    while(item)
    {
        ev = list_get_data(item);
        if(ev->fd == fd)
            break;
        item = list_get_next(item);
    }
    if(!item)
    {
        log_error(LOG_MSG("failed to detach fd=%d [not found]"), fd);
        return;
    }

#ifdef DEBUG
    log_debug(LOG_MSG("detach fd=%d"), fd);
#endif

#if defined(EV_TYPE_KQUEUE)
    int ev_filter = (EVENT_READ == ev->type) ? EVFILT_READ : EVFILT_WRITE;
    EV_ETYPE ke;
    EV_SET(&ke, fd, ev_filter, EV_DELETE, 0, 0, ev);
    ret = kevent(event->efd, &ke, 1, NULL, 0, NULL);
#else
    ret = epoll_ctl(event->efd, EPOLL_CTL_DEL, fd, NULL);
#endif

    if(ret == -1)
        log_error(LOG_MSG("failed to detach fd=%d [%s]")
                  , fd, strerror(errno));

    event->ev_list = list_delete(item, NULL);
    free(ev);
    --event->fd_count;
}

/* kqueue/epoll */
int event_attach(int fd, void (*cb)(void *, int), void *arg
                 , int event_type)
{
#ifdef DEBUG
    log_debug(LOG_MSG("attach fd=%d"), fd);
#endif

    event_item_t *ev = calloc(1, sizeof(event_item_t));
    ev->fd = fd;
    ev->cb = cb;
    ev->arg = arg;
    ev->type = event_type;

    int ret = -1;
    EV_ETYPE ed;

#if defined(EV_TYPE_KQUEUE)
    const int ev_filter = (event_type == EVENT_READ)
                          ? EVFILT_READ : EVFILT_WRITE;
#else
    const int ev_filter = (event_type == EVENT_READ)
                          ? EPOLLIN : EPOLLOUT;
#endif

    int try_count = 0;
    do
    {
#if defined(EV_TYPE_KQUEUE)
        EV_SET(&ed, fd, ev_filter, EV_ADD | EV_FLAGS, EV_FFLAGS, 0, ev);
        ret = kevent(event->efd, &ed, 1, NULL, 0, NULL);
#else
        ed.data.ptr = ev;
        ed.events = ev_filter | EV_FLAGS;
        ret = epoll_ctl(event->efd, EPOLL_CTL_ADD, fd, &ed);
#endif
        if(ret == -1)
        {
            if(errno == EBADF && try_count < 10)
            {
                ++try_count;
                log_warning(LOG_MSG("attach EBADF, try again %d")
                            , try_count);
                usleep(1000);
                continue;
            }

            log_error(LOG_MSG("failed to attach fd=%d [%s]")
                      , fd, strerror(errno));
            free(ev);
            return 0;
        }
        break;
    } while(1);

    event->ev_list = list_append(event->ev_list, ev);

    ++event->fd_count;
    return 1;
}

/* kqueue/epoll */
void event_destroy(void)
{
    if(!event)
        return;

    list_t *i = list_get_first(event->ev_list);
    while(i)
    {
        event_item_t *ev = list_get_data(i);
        ev->cb(ev->arg, EVENT_ERROR);
        i = list_get_first(event->ev_list);
    }

    if(event->efd > 0)
        close(event->efd);

    free(event->ed_list);
    free(event);
    event = NULL;
}

/* kqueue/epoll */
int event_init(void)
{
    event = calloc(1, sizeof(event_t));

#if defined(EV_TYPE_KQUEUE)
    event->efd = kqueue();
#else
    event->efd = epoll_create(1024);
#endif

    if(event->efd < 0)
    {
        log_error(LOG_MSG("failed to init [%s]"), strerror(errno));
        event_destroy();
        return 0;
    }

    return 1;
}

/* kqueue/epoll */
void event_action(void)
{
    static struct timespec tv = { .tv_sec = 0, .tv_nsec = 10000000 };
    if(!event->fd_count)
    {
        nanosleep(&tv, NULL);
        return;
    }

    if(event->fd_count > event->fd_max)
    {
        event->fd_max = ((event->fd_count / EVENT_STEP) + 1) * EVENT_STEP;
        const size_t ed_list_size = sizeof(EV_ETYPE) * event->fd_max;
        if(!event->ed_list)
            event->ed_list = malloc(ed_list_size);
        else
            event->ed_list = realloc(event->ed_list, ed_list_size);
    }

    int ret = -1;
#if defined(EV_TYPE_KQUEUE)
    ret = kevent(event->efd, NULL, 0, event->ed_list, event->fd_count, &tv);
#else
    ret = epoll_wait(event->efd, event->ed_list, event->fd_count, 10);
#endif

    if(ret < 0)
    {
        if(errno != EINTR)
            log_warning(LOG_MSG("action [%s]"), strerror(errno));

        return;
    }

    for(int i = 0; i < ret; i++)
    {
        EV_ETYPE *ed = &event->ed_list[i];
#if defined(EV_TYPE_KQUEUE)
        event_item_t *ev = ed->udata;
        const int ev_check_ok = ((ed->flags & EV_ADD)
                                 && !(ed->fflags & EV_FFLAGS)
                                 && (ed->data > 0));
        const int ev_check_err = (!ev_check_ok
                                  && (ed->flags & ~EV_ADD));
#else
        event_item_t *ev = ed->data.ptr;
        const int ev_check_ok = (!(ed->events & EPOLLERR)
                                 && ed->events & (EPOLLIN | EPOLLOUT));
        const int ev_check_err = (!ev_check_ok
                                  && (ed->events & ~(EPOLLIN | EPOLLOUT)));
#endif
        if(ev_check_ok)
            ev->cb(ev->arg, ev->type);
        if(ev_check_err)
            ev->cb(ev->arg, EVENT_ERROR);
    }
}

#elif defined(EV_TYPE_POLL)

struct event_s
{
    int fd_max;
    int fd_count;

    struct pollfd *ed_list;
    event_item_t *ev_list;
};

/* poll */
void event_detach(int fd)
{
#ifdef DEBUG
    log_debug(LOG_MSG("detach fd=%d"), fd);
#endif

    for(int i = 0; i < event->fd_count; i++)
    {
        if(event->ed_list[i].fd == fd)
        {
            event->ed_list[i].events = 0;
            return;
        }
    }

    log_error(LOG_MSG("failed to detach fd=%d [not found]"), fd);
}

/* poll */
int event_attach(int fd, void (*cb)(void *, int), void *arg, int event_type)
{
#ifdef DEBUG
    log_debug(LOG_MSG("attach fd=%d"), fd);
#endif

    if(event->fd_count >= event->fd_max)
    {
        event->fd_max += EVENT_STEP;
        event->ev_list = realloc(event->ev_list
                                 , sizeof(event_item_t) * event->fd_max);
        event->ed_list = realloc(event->ed_list
                                 , sizeof(struct pollfd) * event->fd_max);
    }

    event_item_t *ev = &event->ev_list[event->fd_count];
    ev->fd = fd;
    ev->cb = cb;
    ev->arg = arg;
    ev->type = event_type;

    struct pollfd *ed = &event->ed_list[event->fd_count];
    ed->events = (event_type == EVENT_READ) ? POLLIN : POLLOUT;
    ed->fd = fd;
    ed->revents = 0;

    ++event->fd_count;
    return 1;
}

/* poll */
void event_destroy(void)
{
    if(!event)
        return;

    for(int i = 0; i < event->fd_count; i++)
    {
        event_item_t *ev = &event->ev_list[i];
        if(event->ed_list[i].events) // check, is callback dettach
            ev->cb(ev->arg, EVENT_ERROR);
    }
    if(event->fd_max > 0)
    {
        free(event->ed_list);
        free(event->ev_list);
    }

    free(event);
    event = NULL;
}

/* poll */
int event_init(void)
{
    event = calloc(1, sizeof(event_t));
    if(event)
        return 1;

    event_destroy();
    return 0;
}

/* poll */
void event_action(void)
{
    static struct timespec tv = { .tv_sec = 0, .tv_nsec = 10000000 };
    if(!event->fd_count)
    {
        nanosleep(&tv, NULL);
        return;
    }

    int ret = poll(event->ed_list, event->fd_count, 10);
    if(ret < 0)
    {
        if(errno != EINTR)
            log_warning(LOG_MSG("action [%s]"), strerror(errno));
        return;
    }

    int i = 0;
    for(; i < event->fd_count && ret > 0; i++)
    {
        int revents = event->ed_list[i].revents;
        if(revents == 0)
            continue;
        --ret;
        event_item_t *ev = &event->ev_list[i];
        if(!event->ed_list[i].events)
            continue;
        if(revents & (POLLIN | POLLOUT))
            ev->cb(ev->arg, ev->type);
        if(!event->ed_list[i].events)
            continue;
        if(revents & (POLLERR | POLLHUP | POLLNVAL))
            ev->cb(ev->arg, EVENT_ERROR);
    }

    // clean detached
    i = 0;
    while(i < event->fd_count)
    {
        if(!event->ed_list[i].events)
        {
            --event->fd_count;
            for(int j = i; j < event->fd_count; j++)
            {
                memcpy(&event->ed_list[j], &event->ed_list[j+1]
                       , sizeof(struct pollfd));
                memcpy(&event->ev_list[j], &event->ev_list[j+1]
                       , sizeof(event_item_t));
            }
        }
        else
            ++i;
    }
}

#elif defined(EV_TYPE_SELECT)

struct event_s
{
    int max_fd;
    fd_set rmaster;
    fd_set wmaster;

    list_t *ev_list;
};

/* select */
void event_detach(int fd)
{
    if(fd <= 0)
        return;

    event_item_t *ev;
    list_t *item = list_get_first(event->ev_list);
    while(item)
    {
        ev = list_get_data(item);
        if(ev->fd == fd)
            break;
        item = list_get_next(item);
    }
    if(!item)
    {
        log_error(LOG_MSG("failed to detach fd=%d [not found]"), fd);
        return;
    }

#ifdef DEBUG
    log_debug(LOG_MSG("detach fd=%d"), fd);
#endif

    if(ev->type == EVENT_READ)
        FD_CLR(fd, &event->rmaster);
    else if(ev->type == EVENT_WRITE)
        FD_CLR(fd, &event->wmaster);

    event->ev_list = list_delete(item, NULL);
    free(ev);

    if(event->max_fd == fd)
    {
        item = list_get_first(event->ev_list);
        int max_fd = 0;
        while(item)
        {
            ev = list_get_data(item);
            if(ev->fd > max_fd)
                max_fd = ev->fd;
            item = list_get_next(item);
        }
        event->max_fd = max_fd;
    }
}

/* select */
int event_attach(int fd, void (*cb)(void *, int), void *arg, int event_type)
{
#ifdef DEBUG
    log_debug(LOG_MSG("attach fd=%d"), fd);
#endif

    event_item_t *ev = calloc(1, sizeof(event_item_t));
    ev->fd = fd;
    ev->cb = cb;
    ev->arg = arg;
    ev->type = event_type;

    if(event_type == EVENT_READ)
        FD_SET(fd, &event->rmaster);
    else if(event_type == EVENT_WRITE)
        FD_SET(fd, &event->wmaster);

    event->ev_list = list_append(event->ev_list, ev);

    if(fd > event->max_fd)
        event->max_fd = fd;
    return 1;
}

/* select */
void event_destroy(void)
{
    if(!event)
        return;

    list_t *i = list_get_first(event->ev_list);
    while(i)
    {
        event_item_t *ev = list_get_data(i);
        ev->cb(ev->arg, EVENT_ERROR);
        i = list_get_first(event->ev_list);
    }

    free(event);
    event = NULL;
}

/* select */
int event_init(void)
{
    event = calloc(1, sizeof(event_t));

    while(event)
    {
        return 1;
    }

    event_destroy();
    return 0;
}

/* select */
void event_action(void)
{
    if(!event->ev_list)
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
    memcpy(&rset, &event->rmaster, sizeof(rset));
    memcpy(&wset, &event->wmaster, sizeof(wset));

    static struct timeval timeout = { .tv_sec = 0, .tv_usec = 10000 };
    int ret = select(event->max_fd + 1, &rset, &wset, NULL, &timeout);
    if(ret < 0)
    {
        if(errno != EINTR)
            log_warning(LOG_MSG("action [%s]"), strerror(errno));
        return;
    }

    list_t *i = list_get_first(event->ev_list);
    while(i)
    {
        event_item_t *ev = list_get_data(i);
        i = list_get_next(i);
        if(ev->type == EVENT_READ)
        {
            if(FD_ISSET(ev->fd, &rset))
                ev->cb(ev->arg, EVENT_READ);
        }
        else if(ev->type == EVENT_WRITE)
        {
            if(FD_ISSET(ev->fd, &wset))
                ev->cb(ev->arg, EVENT_WRITE);
        }
    }
}

#endif
