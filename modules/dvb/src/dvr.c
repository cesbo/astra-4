/*
 * Astra DVB Module
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#include "../dvb.h"
#include <fcntl.h>

static void dvr_on_error(void *arg)
{
    module_data_t *mod = arg;
    asc_log_error(MSG("dvr read error, try to reopen [%s]"), strerror(errno));
    dvr_close(mod);
}

static void dvr_on_read(void *arg)
{
    module_data_t *mod = arg;

    const ssize_t len = read(mod->dvr_fd, mod->dvr_buffer, DVR_BUFFER_SIZE);
    if(len <= 0)
    {
        dvr_on_error(mod);
        return;
    }
    mod->dvr_read += len;

    for(int i = 0; i < len; i += TS_PACKET_SIZE)
    {
        if(mod->ca_ready)
            ca_on_ts(mod, &mod->dvr_buffer[i]);

        module_stream_send(mod, &mod->dvr_buffer[i]);
    }
}

void dvr_open(module_data_t *mod)
{
    char dev_name[32];
    sprintf(dev_name, "/dev/dvb/adapter%d/dvr%d", mod->adapter, mod->device);
    mod->dvr_fd = open(dev_name, O_RDONLY | O_NONBLOCK);
    if(mod->dvr_fd <= 0)
    {
        asc_log_error(MSG("failed to open dvr [%s]"), strerror(errno));
        astra_abort();
    }

    if(mod->dvr_buffer_size > 0)
    {
        const uint64_t buffer_size = mod->dvr_buffer_size * 4096;
        if(ioctl(mod->dvr_fd, DMX_SET_BUFFER_SIZE, buffer_size) < 0)
        {
            asc_log_error(MSG("DMX_SET_BUFFER_SIZE failed [%s]"), strerror(errno));
            astra_abort();
        }
    }

    mod->dvr_event = asc_event_init(mod->dvr_fd, mod);
    asc_event_set_on_read(mod->dvr_event, dvr_on_read);
    asc_event_set_on_error(mod->dvr_event, dvr_on_error);
}

void dvr_close(module_data_t *mod)
{
    mod->dvr_read = 0;

    if(mod->dvr_fd > 0)
    {
        asc_event_close(mod->dvr_event);
        mod->dvr_event = NULL;
        close(mod->dvr_fd);
        mod->dvr_fd = 0;
    }
}
