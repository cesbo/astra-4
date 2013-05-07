/*
 * Astra DVB Module
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#include "../dvb.h"
#include <poll.h>

static void dvb_thread_loop(void *arg)
{
    module_data_t *mod = arg;

    fe_open(mod);
    ca_open(mod);

    nfds_t nfds = 0;

    struct pollfd fds[2];
    memset(fds, 0, sizeof(fds));

    fds[nfds].fd = mod->fe_fd;
    fds[nfds].events = POLLIN;
    ++nfds;

    if(mod->ca_fd)
    {
        fds[nfds].fd = mod->ca_fd;
        fds[nfds].events = POLLIN;
        ++nfds;
    }

    asc_thread_while(mod->thread)
    {
        const int ret = poll(fds, nfds, 1000);
        if(ret > 0)
        {
            if(fds[0].revents)
                fe_loop(mod, fds[0].revents & (POLLPRI | POLLIN));
            if(fds[1].revents)
                ca_loop(mod, fds[1].revents & (POLLPRI | POLLIN));
        }
        else if(ret == 0)
        {
            fe_loop(mod, 0);
            ca_loop(mod, 0);
        }
        else
        {
            asc_log_error(MSG("poll() failed [%s]"), strerror(errno));
            astra_abort();
        }
    }

    fe_close(mod);
    ca_close(mod);
}

void dvb_thread_open(module_data_t *mod)
{
    asc_thread_init(&mod->thread, dvb_thread_loop, mod);
}

void dvb_thread_close(module_data_t *mod)
{
    asc_thread_destroy(&mod->thread);
}
