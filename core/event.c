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

#include "assert.h"
#include "event.h"
#include "list.h"
#include "log.h"

extern bool is_main_loop_idle;

#ifndef EV_LIST_SIZE
#   define EV_LIST_SIZE 1024
#endif

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
#   define MSG(_msg) "[core/event epoll] " _msg
#endif

struct asc_event_t
{
    int fd;
    event_callback_t on_read;
    event_callback_t on_write;
    event_callback_t on_error;
    void *arg;
};

#if defined(EV_TYPE_KQUEUE) || defined(EV_TYPE_EPOLL)

/*
 * ooooooooooo oooooooooo    ooooooo  ooooo       ooooo
 *  888    88   888    888 o888   888o 888         888
 *  888ooo8     888oooo88  888     888 888         888
 *  888    oo   888        888o   o888 888      o  888      o
 * o888ooo8888 o888o         88ooo88  o888ooooo88 o888ooooo88
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *
 * oooo   oooo ooooooo  ooooo  oooo ooooooooooo ooooo  oooo ooooooooooo
 *  888  o88 o888   888o 888    88   888    88   888    88   888    88
 *  888888   888     888 888    88   888ooo8     888    88   888ooo8
 *  888  88o 888o  8o888 888    88   888    oo   888    88   888    oo
 * o888o o888o 88ooo88    888oo88   o888ooo8888   888oo88   o888ooo8888
 *                  88o8
 */

typedef struct
{
    asc_list_t *event_list;
    bool is_changed;

    int fd;
    EV_OTYPE ed_list[EV_LIST_SIZE];
} event_observer_t;

static event_observer_t event_observer;

void asc_event_core_init(void)
{
    memset(&event_observer, 0, sizeof(event_observer));
    event_observer.event_list = asc_list_init();

#if defined(EV_TYPE_KQUEUE)
    event_observer.fd = kqueue();
#else
    event_observer.fd = epoll_create(EV_LIST_SIZE);
#endif

    asc_assert(event_observer.fd != -1
               , MSG("failed to init event observer [%s]")
               , strerror(errno));
}

void asc_event_core_destroy(void)
{
    if(!event_observer.fd)
        return;

    close(event_observer.fd);
    event_observer.fd = 0;

    asc_event_t *prev_event = NULL;
    for(asc_list_first(event_observer.event_list)
        ; !asc_list_eol(event_observer.event_list)
        ; asc_list_first(event_observer.event_list))
    {
        asc_event_t *event = asc_list_data(event_observer.event_list);
        asc_assert(event != prev_event
                   , MSG("loop on asc_event_core_destroy() event:%p")
                   , event);
        if(event->on_error)
            event->on_error(event->arg);
        prev_event = event;
    }

    asc_list_destroy(event_observer.event_list);
    event_observer.event_list = NULL;
}

void asc_event_core_loop(void)
{
    if(!asc_list_size(event_observer.event_list))
        return;

#if defined(EV_TYPE_KQUEUE)
    static const struct timespec timeout = { 0, 0 };
    const int ret = kevent(event_observer.fd, NULL, 0
                           , event_observer.ed_list, EV_LIST_SIZE, &timeout);
#else
    const int ret = epoll_wait(event_observer.fd, event_observer.ed_list, EV_LIST_SIZE, 0);
#endif

    if(ret == -1)
    {
        asc_assert(errno == EINTR, MSG("event observer critical error [%s]"), strerror(errno));
        return;
    }

    event_observer.is_changed = false;
    for(int i = 0; i < ret; ++i)
    {
        EV_OTYPE *ed = &event_observer.ed_list[i];
#if defined(EV_TYPE_KQUEUE)
        asc_event_t *event = ed->udata;
        const bool is_rd = (ed->data > 0) && (ed->filter == EVFILT_READ);
        const bool is_wr = (ed->data > 0) && (ed->filter == EVFILT_WRITE);
        const bool is_er = (ed->flags & ~EV_ADD);
#else
        asc_event_t *event = ed->data.ptr;
        const bool is_rd = ed->events & EPOLLIN;
        const bool is_wr = ed->events & EPOLLOUT;
        const bool is_er = ed->events & EPOLLERR;
#endif
        if(event->on_read && is_rd)
        {
            is_main_loop_idle = false;
            event->on_read(event->arg);
            if(event_observer.is_changed)
                break;
        }
        if(event->on_error && is_er)
        {
            is_main_loop_idle = false;
            event->on_error(event->arg);
            if(event_observer.is_changed)
                break;
        }
        if(event->on_write && is_wr)
        {
            is_main_loop_idle = false;
            event->on_write(event->arg);
            if(event_observer.is_changed)
                break;
        }
    }
}

static void asc_event_subscribe(asc_event_t *event)
{
    int ret = 0;
    EV_OTYPE ed;

#if defined(EV_TYPE_KQUEUE)
    do
    {
        if(event->on_read)
        {
            EV_SET(&ed, event->fd, EVFILT_READ, EV_ADD | EV_EOF | EV_ERROR, 0, 0, event);
            ret = kevent(event_observer.fd, &ed, 1, NULL, 0, NULL);
            if(ret == -1)
                break;
        }
        else
        {
            EV_SET(&ed, event->fd, EVFILT_READ, EV_DELETE, 0, 0, event);
            kevent(event_observer.fd, &ed, 1, NULL, 0, NULL);
        }

        if(event->on_write)
        {
            EV_SET(&ed, event->fd, EVFILT_WRITE, EV_ADD | EV_EOF | EV_ERROR, 0, 0, event);
            ret = kevent(event_observer.fd, &ed, 1, NULL, 0, NULL);
            if(ret == -1)
                break;
        }
        else
        {
            EV_SET(&ed, event->fd, EVFILT_WRITE, EV_DELETE, 0, 0, event);
            kevent(event_observer.fd, &ed, 1, NULL, 0, NULL);
        }

        return;
    } while(0);

#else /* EV_TYPE_EPOLL */

    ed.data.ptr = event;
    ed.events = EV_FLAGS;
    if(event->on_read)
        ed.events |= EPOLLIN;
    if(event->on_write)
        ed.events |= EPOLLOUT;
    ret = epoll_ctl(event_observer.fd, EPOLL_CTL_MOD, event->fd, &ed);
#endif

    asc_assert(ret != -1, MSG("failed to set fd=%d [%s]"), event->fd, strerror(errno));
}

asc_event_t * asc_event_init(int fd, void *arg)
{
    asc_event_t *event = calloc(1, sizeof(asc_event_t));
    event->fd = fd;
    event->arg = arg;

#if defined(EV_TYPE_EPOLL)
    EV_OTYPE ed;
    ed.data.ptr = event;
    ed.events = EV_FLAGS;
    const int ret = epoll_ctl(event_observer.fd, EPOLL_CTL_ADD, event->fd, &ed);
    asc_assert(ret != -1, MSG("failed to attach fd=%d [%s]"), event->fd, strerror(errno));
#endif

    asc_list_insert_tail(event_observer.event_list, event);
    event_observer.is_changed = true;

    return event;
}

void asc_event_close(asc_event_t *event)
{
    if(!event)
        return;

#if defined(EV_TYPE_KQUEUE)
    EV_OTYPE ed;

    if(event->on_read)
    {
        EV_SET(&ed, event->fd, EVFILT_READ, EV_DELETE, 0, 0, event);
        kevent(event_observer.fd, &ed, 1, NULL, 0, NULL);
    }
    if(event->on_write)
    {
        EV_SET(&ed, event->fd, EVFILT_WRITE, EV_DELETE, 0, 0, event);
        kevent(event_observer.fd, &ed, 1, NULL, 0, NULL);
    }

#else /* EV_TYPE_EPOLL */

    epoll_ctl(event_observer.fd, EPOLL_CTL_DEL, event->fd, NULL);
#endif

    event_observer.is_changed = true;
    asc_list_remove_item(event_observer.event_list, event);

    free(event);
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
    asc_event_t *event_list[EV_LIST_SIZE];
    bool is_changed;
    int fd_count;

    struct pollfd fd_list[EV_LIST_SIZE];
} event_observer_t;

#define ED_SIZE (int)(sizeof(struct pollfd))

static event_observer_t event_observer;

void asc_event_core_init(void)
{
    memset(&event_observer, 0, sizeof(event_observer));
}

void asc_event_core_destroy(void)
{
    while(event_observer.fd_count > 0)
    {
        const int next_fd_count = event_observer.fd_count - 1;

        asc_event_t *event = event_observer.event_list[next_fd_count];
        if(event->on_error)
            event->on_error(event->arg);

        asc_assert(event_observer.fd_count == next_fd_count
                   , MSG("loop on asc_event_core_destroy() event:%p")
                   , event);
    }
}

void asc_event_core_loop(void)
{
    if(!event_observer.fd_count)
        return;

    int ret = poll(event_observer.fd_list, event_observer.fd_count, 10);
    if(ret == -1)
    {
        asc_assert(errno == EINTR, MSG("event observer critical error [%s]"), strerror(errno));
        return;
    }

    event_observer.is_changed = false;
    for(int i = 0; i < event_observer.fd_count && ret > 0; ++i)
    {
        const short revents = event_observer.fd_list[i].revents;
        if(revents == 0)
            continue;

        --ret;
        asc_event_t *event = event_observer.event_list[i];
        if(event->on_read && (revents & POLLIN))
        {
            is_main_loop_idle = false;
            event->on_read(event->arg);
            if(event_observer.is_changed)
                break;
        }
        if(event->on_error && (revents & (POLLERR | POLLHUP | POLLNVAL)))
        {
            is_main_loop_idle = false;
            event->on_error(event->arg);
            if(event_observer.is_changed)
                break;
        }
        if(event->on_write && (revents & POLLOUT))
        {
            is_main_loop_idle = false;
            event->on_write(event->arg);
            if(event_observer.is_changed)
                break;
        }
    }
}

static void asc_event_subscribe(asc_event_t *event)
{
    int i;
    for(i = 0; i < event_observer.fd_count; ++i)
    {
        if(event_observer.event_list[i]->fd == event->fd)
            break;
    }
    asc_assert(i < event_observer.fd_count, MSG("filed to set fd=%d"), event->fd);

    event_observer.fd_list[i].events = 0;
    if(event->on_read)
        event_observer.fd_list[i].events |= POLLIN;
    if(event->on_write)
        event_observer.fd_list[i].events |= POLLOUT;
}

asc_event_t * asc_event_init(int fd, void *arg)
{
    const int i = event_observer.fd_count;
    memset(&event_observer.fd_list[i], 0, sizeof(struct pollfd));
    event_observer.fd_list[i].fd = fd;

    asc_event_t *event = calloc(1, sizeof(asc_event_t));
    event_observer.event_list[i] = event;
    event->fd = fd;
    event->arg = arg;

    event_observer.fd_count += 1;
    event_observer.is_changed = true;

    return event;
}

void asc_event_close(asc_event_t *event)
{
    if(!event)
        return;

    int i;
    for(i = 0; i < event_observer.fd_count; ++i)
    {
        if(event_observer.event_list[i]->fd == event->fd)
            break;
    }
    asc_assert(i < event_observer.fd_count, MSG("filed to detach fd=%d"), event->fd);

    for(; i < event_observer.fd_count; ++i)
    {
        memcpy(&event_observer.fd_list[i], &event_observer.fd_list[i + 1]
               , sizeof(struct pollfd));
        event_observer.event_list[i] = event_observer.event_list[i + 1];
    }
    memset(&event_observer.fd_list[i], 0, sizeof(struct pollfd));
    event_observer.event_list[i] = NULL;

    event_observer.fd_count -= 1;
    event_observer.is_changed = true;

    free(event);
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
    asc_list_t *event_list;
    bool is_changed;

    int max_fd;
    fd_set rmaster;
    fd_set wmaster;
    fd_set emaster;
} event_observer_t;

static event_observer_t event_observer;

void asc_event_core_init(void)
{
    memset(&event_observer, 0, sizeof(event_observer));
    event_observer.event_list = asc_list_init();
}

void asc_event_core_destroy(void)
{
    asc_event_t *prev_event = NULL;
    for(asc_list_first(event_observer.event_list)
        ; !asc_list_eol(event_observer.event_list)
        ; asc_list_first(event_observer.event_list))
    {
        asc_event_t *event = asc_list_data(event_observer.event_list);
        asc_assert(event != prev_event
                   , MSG("loop on asc_event_core_destroy() event:%p")
                   , event);
        if(event->on_error)
            event->on_error(event->arg);
        prev_event = event;
    }

    asc_list_destroy(event_observer.event_list);
    event_observer.event_list = NULL;
}

void asc_event_core_loop(void)
{
    if(!asc_list_size(event_observer.event_list))
        return;

    fd_set rset;
    fd_set wset;
    fd_set eset;
    memcpy(&rset, &event_observer.rmaster, sizeof(rset));
    memcpy(&wset, &event_observer.wmaster, sizeof(wset));
    memcpy(&eset, &event_observer.emaster, sizeof(eset));

    static struct timeval timeout = { .tv_sec = 0, .tv_usec = 0 };
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
        event_observer.is_changed = false;
        asc_list_for(event_observer.event_list)
        {
            asc_event_t *event = asc_list_data(event_observer.event_list);
            if(event->on_read && FD_ISSET(event->fd, &rset))
            {
                is_main_loop_idle = false;
                event->on_read(event->arg);
                if(event_observer.is_changed)
                    break;
            }
            if(event->on_error && FD_ISSET(event->fd, &eset))
            {
                is_main_loop_idle = false;
                event->on_error(event->arg);
                if(event_observer.is_changed)
                    break;
            }
            if(event->on_write && FD_ISSET(event->fd, &wset))
            {
                is_main_loop_idle = false;
                event->on_write(event->arg);
                if(event_observer.is_changed)
                    break;
            }
        }
    }
}

static void asc_event_subscribe(asc_event_t *event)
{
    if(event->on_read)
        FD_SET(event->fd, &event_observer.rmaster);
    else
        FD_CLR(event->fd, &event_observer.rmaster);

    if(event->on_write)
        FD_SET(event->fd, &event_observer.wmaster);
    else
        FD_CLR(event->fd, &event_observer.wmaster);

    if(event->on_error)
        FD_SET(event->fd, &event_observer.emaster);
    else
        FD_CLR(event->fd, &event_observer.emaster);
}

asc_event_t * asc_event_init(int fd, void *arg)
{
    asc_event_t *event = calloc(1, sizeof(asc_event_t));
    event->fd = fd;
    event->arg = arg;

    if(fd > event_observer.max_fd)
        event_observer.max_fd = fd;

    asc_list_insert_tail(event_observer.event_list, event);
    event_observer.is_changed = true;

    return event;
}

void asc_event_close(asc_event_t *event)
{
    if(!event)
        return;

    event_observer.is_changed = true;

    event->on_read = NULL;
    event->on_write = NULL;
    event->on_error = NULL;
    asc_event_subscribe(event);

    if(event->fd < event_observer.max_fd)
    {
        asc_list_remove_item(event_observer.event_list, event);
        free(event);
        return;
    }

    event_observer.max_fd = 0;
    for(asc_list_first(event_observer.event_list)
        ; !asc_list_eol(event_observer.event_list)
        ; )
    {
        asc_event_t *i_event = asc_list_data(event_observer.event_list);
        if(i_event == event)
        {
            asc_list_remove_current(event_observer.event_list);
            free(event);
        }
        else
        {
            if(i_event->fd > event_observer.max_fd)
                event_observer.max_fd = i_event->fd;
            asc_list_next(event_observer.event_list);
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

void asc_event_set_on_read(asc_event_t *event, event_callback_t on_read)
{
    if(event->on_read == on_read)
        return;

    event->on_read = on_read;
    asc_event_subscribe(event);
}

void asc_event_set_on_write(asc_event_t *event, event_callback_t on_write)
{
    if(event->on_write == on_write)
        return;

    event->on_write = on_write;
    asc_event_subscribe(event);
}

void asc_event_set_on_error(asc_event_t *event, event_callback_t on_error)
{
    if(event->on_error == on_error)
        return;

    event->on_error = on_error;
    asc_event_subscribe(event);
}
