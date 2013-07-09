/*
 * Astra DVB Module
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#include "../dvb.h"
#include <fcntl.h>

static void __dmx_join_pid(module_data_t *mod, int fd, uint16_t pid)
{
    struct dmx_pes_filter_params pes_filter;
    memset(&pes_filter, 0, sizeof(pes_filter));
    pes_filter.pid = pid;
    pes_filter.input = DMX_IN_FRONTEND;
    pes_filter.output = DMX_OUT_TS_TAP;
    pes_filter.pes_type = DMX_PES_OTHER;
    pes_filter.flags = DMX_IMMEDIATE_START;

    if(ioctl(fd, DMX_SET_PES_FILTER, &pes_filter) < 0)
    {
        asc_log_error(MSG("DMX_SET_PES_FILTER failed [%s]"), strerror(errno));
        astra_abort();
    }
}

static int __dmx_open(module_data_t *mod)
{
    const int fd = open(mod->dmx_dev_name, O_WRONLY);
    if(fd <= 0)
    {
        asc_log_error(MSG("failed to open demux [%s]"), strerror(errno));
        astra_abort();
    }
    return fd;
}

void dmx_set_pid(module_data_t *mod, uint16_t pid, int is_set)
{
    if(mod->dmx_budget)
        return;

    if(pid >= MAX_PID)
    {
        asc_log_error(MSG("demux: PID value must be less then %d"), __FUNCTION__, MAX_PID);
        astra_abort();
    }

    if(is_set)
    {
        if(!mod->dmx_fd_list[pid])
        {
            mod->dmx_fd_list[pid] = __dmx_open(mod);
            __dmx_join_pid(mod, mod->dmx_fd_list[pid], pid);
        }
    }
    else
    {
        if(mod->dmx_fd_list[pid])
        {
            close(mod->dmx_fd_list[pid]);
            mod->dmx_fd_list[pid] = 0;
        }
    }
}

void dmx_bounce(module_data_t *mod)
{
    const int fd_max = (mod->dmx_budget) ? 1 : MAX_PID;
    for(int i = 0; i < fd_max; ++i)
    {
        if(mod->dmx_fd_list[i])
        {
            ioctl(mod->dmx_fd_list[i], DMX_STOP);
            ioctl(mod->dmx_fd_list[i], DMX_START);
        }
    }
}

void dmx_open(module_data_t *mod)
{
    sprintf(mod->dmx_dev_name, "/dev/dvb/adapter%d/demux%d", mod->adapter, mod->device);

    const int fd = __dmx_open(mod);
    if(mod->dmx_budget)
    {
        mod->dmx_fd_list = calloc(1, sizeof(int));
        mod->dmx_fd_list[0] = fd;
        __dmx_join_pid(mod, fd, MAX_PID);
    }
    else
    {
        close(fd);
        mod->dmx_fd_list = calloc(MAX_PID, sizeof(int));
    }
}

void dmx_close(module_data_t *mod)
{
    const int fd_max = (mod->dmx_budget) ? 1 : MAX_PID;
    for(int i = 0; i < fd_max; ++i)
    {
        if(mod->dmx_fd_list[i])
            close(mod->dmx_fd_list[i]);
    }
    free(mod->dmx_fd_list);
}
