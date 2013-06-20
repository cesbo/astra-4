/*
 * Astra DVB-ASI Module
 * http://cesbo.com
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#include <astra.h>

#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#define MSG(_msg) "[asi_input %d] " _msg, mod->adapter

#define ASI_BUFFER_SIZE (1022 * TS_PACKET_SIZE)
#define ASI_IOC_RXSETPF _IOW('?', 76, unsigned int [256])

struct module_data_t
{
    MODULE_LUA_DATA();
    MODULE_STREAM_DATA();

    int adapter;
    int budget;

    int fd;
    asc_event_t *event;
    uint8_t filter[MAX_PID / 8];

    uint8_t buffer[ASI_BUFFER_SIZE];
};

static void asi_on_error(void *arg)
{
    module_data_t *mod = arg;

    asc_log_error(MSG("asi read error [%s]"), strerror(errno));
    asc_event_close(mod->event);
    close(mod->fd);
    astra_abort();
}


static void asi_on_read(void *arg)
{
    module_data_t *mod = arg;

    const ssize_t len = read(mod->fd, mod->buffer, ASI_BUFFER_SIZE);
    if(len <= 0)
    {
        asi_on_error(mod);
        return;
    }

    for(int i = 0; i < len; i += TS_PACKET_SIZE)
        module_stream_send(mod, &mod->buffer[i]);
}


static void set_pid(module_data_t *mod, uint16_t pid, int is_set)
{
    if(mod->budget)
        return;

    if(pid >= MAX_PID)
    {
        asc_log_error(MSG("PID value must be less then %d"), __FUNCTION__, MAX_PID);
        astra_abort();
    }

    if(is_set)
        mod->filter[pid / 8] |= (0x01 << (pid % 8));
    else
        mod->filter[pid / 8] &= ~(0x01 << (pid % 8));

    if(ioctl(mod->fd, ASI_IOC_RXSETPF, mod->filter) < 0)
    {
        asc_log_error(MSG("failed to set PES filter [%s]"), strerror(errno));
    }
}

static void join_pid(module_data_t *mod, uint16_t pid)
{
    set_pid(mod, pid, 1);
}

static void leave_pid(module_data_t *mod, uint16_t pid)
{
    set_pid(mod, pid, 0);
}

static void module_init(module_data_t *mod)
{
    module_stream_init(mod, NULL);
    module_stream_demux_set(mod, join_pid, leave_pid);

    if(!module_option_number("adapter", &mod->adapter))
    {
        asc_log_error("[asi_input] option 'adapter' is required");
        astra_abort();
    }
    module_option_number("budget", &mod->budget);

    char dev_name[16];
    sprintf(dev_name, "/dev/asirx%d", mod->adapter);
    mod->fd = open(dev_name, O_RDONLY);
    if(mod->fd <= 0)
    {
        asc_log_error(MSG("failed to open device %s [%s]"), dev_name, strerror(errno));
        mod->fd = 0;
        return;
    }

    if(!mod->budget)
    { /* hw filtering */
        memset(mod->filter, 0x00, sizeof(mod->filter));
    }
    else
    {
        memset(mod->filter, 0xFF, sizeof(mod->filter));
        mod->filter[NULL_TS_PID / 8] &= ~(0x01 << (NULL_TS_PID % 8));
    }

    if(ioctl(mod->fd, ASI_IOC_RXSETPF, mod->filter) < 0)
        asc_log_error(MSG("failed to set PES filter [%s]"), strerror(errno));

    fsync(mod->fd);

    mod->event = asc_event_init(mod->fd, mod);
    asc_event_set_on_read(mod->event, asi_on_read);
    asc_event_set_on_error(mod->event, asi_on_error);
}

static void module_destroy(module_data_t *mod)
{
    module_stream_destroy(mod);

    asc_event_close(mod->event);
    if(mod->fd)
        close(mod->fd);
}


MODULE_STREAM_METHODS()

MODULE_LUA_METHODS()
{
    MODULE_STREAM_METHODS_REF()
};

MODULE_LUA_REGISTER(asi_input)
