/*
 * Astra Core
 * http://cesbo.com
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Revised 2013 Krasheninnikov Alexander
 * Licensed under the MIT license.
 */

#include "assert.h"
#include "event.h"
#include "log.h"
#include "vector.h"

#ifdef _WIN32
#   include <windows.h>
#   include <winsock2.h>
#endif

#   ifndef EV_LIST_SIZE
#       define EV_LIST_SIZE 1024
#   endif

#if defined(WITH_POLL)
#   define EV_TYPE_POLL
#   define MSG(_msg) "[core/event poll] " _msg
#   include <poll.h>
#elif defined(WITH_SELECT) || defined(_WIN32)
#   define EV_TYPE_SELECT
#   define MSG(_msg) "[core/event select] " _msg
#elif !defined(WITH_CUSTOM) && (defined(__APPLE__) || defined(__FreeBSD__))
#   define EV_TYPE_KQUEUE
#   include <sys/event.h>
#   define EV_OTYPE struct kevent
#   define EV_FLAGS (EV_EOF | EV_ERROR)
#   define EV_FFLAGS (NOTE_DELETE | NOTE_RENAME | NOTE_EXTEND)
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
#endif

struct asc_event_t
{
    int fd;
    event_callback_t callback_read;
    event_callback_t callback_write;
    event_callback_t callback_error; /* TODO: Will work properly only if one of read/write
                                      * is assigned.
                                      * NOTE!!! Callback_error should destroy event!
                                      */
    void *arg;
};

static void __asc_event_core_init_common(void);
static void __asc_event_core_destroy_common(void);

/* Returns true if event vec changes */
static bool __asc_event_process(asc_event_t * event, bool is_rd, bool is_wr, bool is_er);


/*
 * ooooooooooo oooooooooo    ooooooo  ooooo       ooooo
 *  888    88   888    888 o888   888o 888         888
 *  888ooo8     888oooo88  888     888 888         888
 *  888    oo   888        888o   o888 888      o  888      o
 * o888ooo8888 o888o         88ooo88  o888ooooo88 o888ooooo88
 *
 */
#if defined(EV_TYPE_KQUEUE) || defined(EV_TYPE_EPOLL)

typedef struct
{
    /* Common */
    asc_ptrvector_t * event_vec;
    bool event_vec_changed;

    /* EPOLL specific */

    int fd;
    EV_OTYPE ed_list[EV_LIST_SIZE];
} event_observer_t;

static event_observer_t event_observer;

void asc_event_core_init(void)
{
    __asc_event_core_init_common();

#if defined(EV_TYPE_KQUEUE)
    event_observer.fd = kqueue();
#else
    event_observer.fd = epoll_create(EV_LIST_SIZE);
#endif

    asc_assert(event_observer.fd != -1, MSG("failed to init event observer [%s]")
               , strerror(errno));
}

void asc_event_core_destroy(void)
{
    if(!event_observer.fd)
        return;

    close(event_observer.fd);
    event_observer.fd = 0;
    __asc_event_core_destroy_common();
}

void asc_event_core_loop(void)
{
    static struct timespec tv = { 0, 10000000 };
    if(asc_ptrvector_count(event_observer.event_vec) <= 0)
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
        asc_assert(errno == EINTR, MSG("event observer critical error [%s]"), strerror(errno));
        return;
    }

    event_observer.event_vec_changed = false;
    for(int i = 0; i < ret; ++i)
    {
        EV_OTYPE *ed = &event_observer.ed_list[i];
#if defined(EV_TYPE_KQUEUE)
        asc_event_t *event = ed->udata;
        const bool is_probably_rdwr = (ed->flags & EV_ADD)
                                      && !(ed->fflags & EV_FFLAGS)
                                      && (ed->data > 0);
        const bool is_rd = is_probably_rdwr && (ed->filter == EVFILT_READ);
        const bool is_wr = is_probably_rdwr && (ed->filter == EVFILT_WRITE);
        const bool is_er = (!is_rd && !is_wr && (ed->flags & ~EV_ADD));
#else
        asc_event_t *event = ed->data.ptr;
        const bool is_rd = ed->events & EPOLLIN;
        const bool is_wr = ed->events & EPOLLOUT;
        const bool is_er = ed->events & EPOLLERR;
#endif
        if(__asc_event_process(event, is_rd, is_wr, is_er))
            break; /* Go out of cycle due to vector change */
    }
}

#if defined(EV_TYPE_KQUEUE)
static void asc_event_subscribe(asc_event_t * event, bool is_add, bool is_delete)
{
    __uarg(is_add);

    asc_assert((!event->callback_error)
               || (event->callback_error && (event->callback_read || event->callback_write))
               , MSG("Should specify READ or WRITE for event if specified ERROR! fd=%d")
               , event->fd);

    int ret;
    EV_OTYPE ed;
    if(event->callback_read && !is_delete)
    {
        EV_SET(&ed, event->fd, EVFILT_READ, EV_ADD | EV_FLAGS, EV_FFLAGS, 0, event);
        ret = kevent(event_observer.fd, &ed, 1, NULL, 0, NULL);
        asc_assert(ret != -1, MSG("failed to attach (read) fd=%d [%s]")
                   , event->fd, strerror(errno));
    }
    else
    {
        EV_SET(&ed, event->fd, EVFILT_READ, EV_DELETE, 0, 0, event);
        ret = kevent(event_observer.fd, &ed, 1, NULL, 0, NULL);
        /* Result is not checked, because it could happen,
         * that there is nothing to delete, but we delete
         */
    }

    if(event->callback_write && !is_delete)
    {
        EV_SET(&ed, event->fd, EVFILT_WRITE, EV_ADD | EV_FLAGS, EV_FFLAGS, 0, event);
        ret = kevent(event_observer.fd, &ed, 1, NULL, 0, NULL);
        asc_assert(ret != -1, MSG("failed to attach (write) fd=%d [%s]")
                   , event->fd, strerror(errno));
    }
    else
    {
        EV_SET(&ed, event->fd, EVFILT_WRITE, EV_DELETE, 0, 0, event);
        ret = kevent(event_observer.fd, &ed, 1, NULL, 0, NULL);
        /* Result is not checked, because it could happen,
         * that there is nothing to delete, but we delete
         */
    }
}

#else /* EPOLL */
static void asc_event_subscribe(asc_event_t * event, bool is_add, bool is_delete)
{
    asc_assert((!event->callback_error)
               || (event->callback_error && (event->callback_read || event->callback_write))
               , MSG("Should specify READ or WRITE for event if specified ERROR! fd=%d")
               , event->fd);

    int ret;
    if(is_delete)
    {
        ret = epoll_ctl(event_observer.fd, EPOLL_CTL_DEL, event->fd, NULL);
    }
    else
    {
        EV_OTYPE ed;
        ed.data.ptr = event;
        ed.events = EV_FLAGS;
        if(event->callback_read)
            ed.events |= EPOLLIN;
        if(event->callback_write)
            ed.events |= EPOLLOUT;
        ret = epoll_ctl(event_observer.fd, (is_add) ? EPOLL_CTL_ADD : EPOLL_CTL_MOD
                        , event->fd, &ed);
    }
    asc_assert(ret != -1, MSG("failed to attach fd=%d [%s]"), event->fd, strerror(errno));
}
#endif

/*
 * oooooooooo    ooooooo  ooooo       ooooo
 *  888    888 o888   888o 888         888
 *  888oooo88  888     888 888         888
 *  888        888o   o888 888      o  888      o
 * o888o         88ooo88  o888ooooo88 o888ooooo88
 *
 */

#elif defined(EV_TYPE_POLL)

typedef struct
{
    /* Common */
    asc_ptrvector_t * event_vec;
    bool event_vec_changed;

    /* POLL specific */
    asc_vector_t * ed_vec;/* holds vector of struct pollfd */
} event_observer_t;

#define ED_SIZE (int)(sizeof(struct pollfd))

static event_observer_t event_observer;

void asc_event_core_init(void)
{
    __asc_event_core_init_common();
    event_observer.ed_vec = asc_vector_init(sizeof(struct pollfd));
}

void asc_event_core_destroy(void)
{
    if(!event_observer.ed_vec)
        return;
    asc_vector_destroy(event_observer.ed_vec);
    event_observer.ed_vec = NULL;

    __asc_event_core_destroy_common();
}

void asc_event_core_loop(void)
{
    static struct timespec tv = { 0, 10000000 };
    if(asc_ptrvector_count(event_observer.event_vec) <= 0)
    {
        nanosleep(&tv, NULL);
        return;
    }

    int count = asc_vector_count(event_observer.ed_vec);
    int ret = poll(asc_vector_get_dataptr(event_observer.ed_vec), count, 10);
    if(ret == -1)
    {
        asc_assert(errno == EINTR, MSG("event observer critical error [%s]"), strerror(errno));
        return;
    }

    int i;

    event_observer.event_vec_changed = false;

    for(i = 0; i < count && ret > 0; ++i)
    {
        struct pollfd * f = asc_vector_get_dataptr_at(event_observer.ed_vec, i);
        const int revents = f->revents;
        if(revents == 0)
            continue;
        --ret;
        asc_event_t *event = asc_ptrvector_get_at(event_observer.event_vec, i);
        if(__asc_event_process(event, revents & POLLIN, revents & POLLOUT
                               , revents & (POLLERR | POLLHUP | POLLNVAL)))
        {
            break; /* Vector change */
        }
    }
}

static void asc_event_subscribe(asc_event_t * event, bool is_add, bool is_delete)
{
    int pos;
    if(is_add)
    {
        pos = asc_vector_count(event_observer.ed_vec);
        struct pollfd tmp;
        tmp.events = 0;
        tmp.fd = event->fd;
        tmp.revents = 0;
        asc_vector_append_end(event_observer.ed_vec, &tmp, 1);
    }
    else
    { /* Need to find a place in ed_vec and event_vec */
        int count = asc_ptrvector_count(event_observer.event_vec);
        bool found = false;
        for(pos = 0; pos < count; ++pos)
        {
            if(asc_ptrvector_get_at(event_observer.event_vec, pos) == event)
            {
                found = true;
                break;
            }
        }
        asc_assert(found, MSG("failed to find event in vector"));
    }
    if(is_delete)
    {
        asc_vector_remove_middle(event_observer.ed_vec, pos, 1);
        return;
    }

    struct pollfd * f = asc_vector_get_dataptr_at(event_observer.ed_vec, pos);
    f->events = 0;
    if(event->callback_read)
        f->events |= POLLIN;
    if(event->callback_write)
        f->events |= POLLOUT;
}

/*
 *  oooooooo8 ooooooooooo ooooo       ooooooooooo  oooooooo8 ooooooooooo
 * 888         888    88   888         888    88 o888     88 88  888  88
 *  888oooooo  888ooo8     888         888ooo8   888             888
 *         888 888    oo   888      o  888    oo 888o     oo     888
 * o88oooo888 o888ooo8888 o888ooooo88 o888ooo8888 888oooo88     o888o
 *
 */

#elif defined(EV_TYPE_SELECT)

typedef struct
{
    /* Common */
    asc_ptrvector_t * event_vec;
    bool event_vec_changed;

    /* SELECT specific */
    int max_fd;
    fd_set rmaster;
    fd_set wmaster;
    fd_set emaster;
} event_observer_t;

static event_observer_t event_observer;

void asc_event_core_init(void)
{
    __asc_event_core_init_common();
}

void asc_event_core_destroy(void)
{
    __asc_event_core_destroy_common();
}

void asc_event_core_loop(void)
{
    if(asc_ptrvector_count(event_observer.event_vec) <= 0)
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
    fd_set eset;
    memcpy(&rset, &event_observer.rmaster, sizeof(rset));
    memcpy(&wset, &event_observer.wmaster, sizeof(wset));
    memcpy(&eset, &event_observer.emaster, sizeof(eset));

    static struct timeval timeout = { .tv_sec = 0, .tv_usec = 10000 };
    const int ret = select(event_observer.max_fd + 1, &rset, &wset, &eset, &timeout);
    if(ret == -1)
    {
#ifdef _WIN32
        int err = WSAGetLastError();
        asc_assert(false, MSG("event observer critical error [WSALastErr: %d]"), err);
#else
        asc_assert(errno == EINTR, MSG("event observer critical error [%s]"), strerror(errno));
#endif
        return;
    }
    else if(ret > 0)
    {
        int sz = asc_ptrvector_count(event_observer.event_vec);
        event_observer.event_vec_changed = false;
        for(int i = 0; i < sz; ++i)
        {
            asc_event_t *event = asc_ptrvector_get_at(event_observer.event_vec, i);
            if(__asc_event_process(event, FD_ISSET(event->fd, &rset)
                                   , FD_ISSET(event->fd, &wset), FD_ISSET(event->fd, &eset)))
            {
                break; /* Vector change */
            }
        }
    }
}

static void asc_event_subscribe(asc_event_t * event, bool is_add, bool is_delete)
{
    if(event->callback_read && !is_delete)
        FD_SET(event->fd, &event_observer.rmaster);
    else
        FD_CLR(event->fd, &event_observer.rmaster);

    if(event->callback_write && !is_delete)
        FD_SET(event->fd, &event_observer.wmaster);
    else
        FD_CLR(event->fd, &event_observer.wmaster);

    if(event->callback_error && !is_delete)
        FD_SET(event->fd, &event_observer.emaster);
    else
        FD_CLR(event->fd, &event_observer.emaster);

    if(is_delete)
    {
        /* now recalc maximum FD */
        int sz = asc_ptrvector_count(event_observer.event_vec);
        event_observer.max_fd = 0;
        for(int i = 0; i < sz; ++i)
        {
            asc_event_t *event = asc_ptrvector_get_at(event_observer.event_vec, i);
            if(event->fd > event_observer.max_fd)
                event_observer.max_fd = event->fd;
        }
    }
    else
    {
        if(is_add)
        {
            if(event->fd > event_observer.max_fd)
                event_observer.max_fd = event->fd;
        }
    }
}

#endif

/*
 *   oooooooo8   ooooooo  oooo     oooo oooo     oooo  ooooooo  oooo   oooo
 * o888     88 o888   888o 8888o   888   8888o   888 o888   888o 8888o  88
 * 888         888     888 88 888o8 88   88 888o8 88 888     888 88 888o88
 * 888o     oo 888o   o888 88  888  88   88  888  88 888o   o888 88   8888
 *  888oooo88    88ooo88  o88o  8  o88o o88o  8  o88o  88ooo88  o88o    88
 *
 */

asc_event_t * asc_event_init(int fd
                             , event_callback_t callback_read
                             , event_callback_t callback_write
                             , event_callback_t callback_error
                             , void *arg)
{
#ifdef DEBUG
    asc_log_debug(MSG("asc_event_init for fd=%d, %c%c%c")
                   , fd
                   , (callback_read) ? 'R' : '-'
                   , (callback_write) ? 'W' : '-'
                   , (callback_error) ? 'E' : '-');
#endif
    asc_event_t *event = malloc(sizeof(asc_event_t));
    event->fd = fd;
    event->callback_read = callback_read;
    event->callback_write = callback_write;
    event->callback_error = callback_error;
    event->arg = arg;
    asc_event_subscribe(event, true, false);
    asc_ptrvector_append_end(event_observer.event_vec, event);
    event_observer.event_vec_changed = true;
    return event;
}

void asc_event_set_read(asc_event_t * event, event_callback_t callback_read)
{
    if (event->callback_read == callback_read) return;
#ifdef DEBUG
    asc_log_debug(MSG("asc_event_set_read for fd=%d, %c")
                  , event->fd
                  , (callback_read) ? 'R' : '-');
#endif
    event->callback_read = callback_read;
    asc_event_subscribe(event, false, false);
}

void asc_event_set_write(asc_event_t * event, event_callback_t callback_write)
{
    if (event->callback_write == callback_write) return;
#ifdef DEBUG
    asc_log_debug(MSG("asc_event_set_write for fd=%d, %c")
                  , event->fd
                  , (callback_write) ? 'W' : '-');
#endif
    event->callback_write = callback_write;
    asc_event_subscribe(event, false, false);
}

void asc_event_set_error(asc_event_t * event, event_callback_t callback_error)
{
    if (event->callback_error == callback_error) return;
#ifdef DEBUG
    asc_log_debug(MSG("asc_event_set_error for fd=%d, %c")
                  , event->fd
                  , (callback_error) ? 'E' : '-');
#endif
    event->callback_error = callback_error;
    asc_event_subscribe(event, false, false);
}

void asc_event_set_arg(asc_event_t * event, void *arg)
{
    event->arg = arg;
}

void asc_event_close(asc_event_t *event)
{
    if(!event)
        return;

#ifdef DEBUG
    asc_log_debug(MSG("detach fd=%d"), event->fd);
#endif
    asc_event_subscribe(event, false, true);
    event_observer.event_vec_changed = true;

    /* Need to find this event in vector of events */
    int sz = asc_ptrvector_count(event_observer.event_vec);
    bool found = false;
    for (int i = 0; i < sz; ++i)
    {
        asc_event_t *event1 = asc_ptrvector_get_at(event_observer.event_vec, i);
        if(event1 == event)
        {
            asc_ptrvector_remove_middle(event_observer.event_vec, i);
            found = true;
            break;
        }
    }
    asc_assert(found, MSG("Event %p not found during event_close"), (void*)event);

    free(event);
}

static void __asc_event_core_init_common(void)
{
    memset(&event_observer, 0, sizeof(event_observer));
    event_observer.event_vec_changed = false;
    event_observer.event_vec = asc_ptrvector_init();
}

static void __asc_event_core_destroy_common(void)
{
    while(1)
    {
        int count = asc_ptrvector_count(event_observer.event_vec);
        if(!count)
            break;

        asc_event_t *event = asc_ptrvector_get_at(event_observer.event_vec, 0);
        if(event->callback_error) /* Callback error SHOULD destroy event! */
            event->callback_error(event->arg);
        else /* Help them! */
            asc_event_close(event);

        asc_assert(asc_ptrvector_count(event_observer.event_vec) < count
                   , MSG("Events should go away during destroy, but they do not... [event %p]")
                   , (void*) event);
    }
    asc_ptrvector_destroy(event_observer.event_vec);
    event_observer.event_vec = NULL;
}

/* Returns true if event vec changes */
static bool __asc_event_process(asc_event_t * event, bool is_rd, bool is_wr, bool is_er)
{
    if(event->callback_read && is_rd)
    {
        event->callback_read(event->arg);
        if(event_observer.event_vec_changed)
            return true;
    }

    if(event->callback_write && is_wr)
    {
        event->callback_write(event->arg);
        if(event_observer.event_vec_changed)
            return true;
    }

    if(event->callback_error && is_er)
    {
        event->callback_error(event->arg);
        if(event_observer.event_vec_changed)
            return true;
    }

    return false;/* No changes in event vec */
}

