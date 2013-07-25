/*
 * Astra DVB Module
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#include "dvb.h"
#include "src/ca.h"

#include <fcntl.h>
#include <poll.h>

#define MSG(_msg) "[dvb_ca %d:%d] " _msg, mod->adapter, mod->device
#define SEC_BUFFER_SIZE (1022 * TS_PACKET_SIZE)

#define BUFFER_SIZE (10 * TS_PACKET_SIZE)

struct module_data_t
{
    MODULE_LUA_DATA();
    MODULE_STREAM_DATA();

    int adapter;
    int device;

    /* Base */
    asc_thread_t *thread;
    bool thread_ready;

    /* */
    dvb_ca_t *ca;

    /* SEC Base */
    int sec_fd;
    asc_event_t *sec_event;

    /* */
    uint32_t enc_buffer_skip;
    uint8_t enc_buffer[BUFFER_SIZE];

    /* */
    uint8_t dec_buffer[BUFFER_SIZE];
};

/*
 *  oooooooo8 ooooooooooo  oooooooo8
 * 888         888    88 o888     88
 *  888oooooo  888ooo8   888
 *         888 888    oo 888o     oo
 * o88oooo888 o888ooo8888 888oooo88
 *
 */

static void sec_close(module_data_t *mod);

static void sec_on_error(void *arg)
{
    module_data_t *mod = arg;
    asc_log_error(MSG("sec read error, try to reopen [%s]"), strerror(errno));
    sec_close(mod);
}

static void sec_on_read(void *arg)
{
    module_data_t *mod = arg;
    const ssize_t len = read(mod->sec_fd, mod->dec_buffer, BUFFER_SIZE);
    if(len <= 0)
    {
        sec_on_error(mod);
        return;
    }
    printf("%s(): len:%ld\n", __FUNCTION__, len);
    for(int i = 0; i < len; i += TS_PACKET_SIZE)
        module_stream_send(mod, &mod->dec_buffer[i]);
}

static void sec_open(module_data_t *mod)
{
    char dev_name[32];
    sprintf(dev_name, "/dev/dvb/adapter%d/sec%d", mod->adapter, mod->device);
    mod->sec_fd = open(dev_name, O_RDWR | O_NONBLOCK);
    if(mod->sec_fd <= 0)
    {
        asc_log_error(MSG("failed to open sec [%s]"), strerror(errno));
        astra_abort();
    }

    mod->sec_event = asc_event_init(mod->sec_fd, mod);
    asc_event_set_on_read(mod->sec_event, sec_on_read);
    asc_event_set_on_error(mod->sec_event, sec_on_error);
}

static void sec_close(module_data_t *mod)
{
    if(mod->sec_fd > 0)
    {
        asc_event_close(mod->sec_event);
        mod->sec_event = NULL;
        close(mod->sec_fd);
        mod->sec_fd = 0;
    }
}

/*
 * ooooooooooo ooooo ooooo oooooooooo  ooooooooooo      o      ooooooooo
 * 88  888  88  888   888   888    888  888    88      888      888    88o
 *     888      888ooo888   888oooo88   888ooo8       8  88     888    888
 *     888      888   888   888  88o    888    oo    8oooo88    888    888
 *    o888o    o888o o888o o888o  88o8 o888ooo8888 o88o  o888o o888ooo88
 *
 */

static void dvb_thread_loop(void *arg)
{
    module_data_t *mod = arg;

    ca_open(mod->ca);
    if(!mod->ca->ca_fd)
        astra_abort();

    nfds_t nfds = 0;

    struct pollfd fds[1];
    memset(fds, 0, sizeof(fds));

    fds[nfds].fd = mod->ca->ca_fd;
    fds[nfds].events = POLLIN;
    ++nfds;

    mod->thread_ready = true;

    asc_thread_while(mod->thread)
    {
        const int ret = poll(fds, nfds, 1000);
        if(ret > 0)
        {
            if(fds[0].revents)
                ca_loop(mod->ca, fds[0].revents & (POLLPRI | POLLIN));
        }
        else if(ret == 0)
        {
            ca_loop(mod->ca, 0);
        }
        else
        {
            asc_log_error(MSG("poll() failed [%s]"), strerror(errno));
            astra_abort();
        }
    }

    ca_close(mod->ca);
}

/*
 * oooo     oooo  ooooooo  ooooooooo  ooooo  oooo ooooo       ooooooooooo
 *  8888o   888 o888   888o 888    88o 888    88   888         888    88
 *  88 888o8 88 888     888 888    888 888    88   888         888ooo8
 *  88  888  88 888o   o888 888    888 888    88   888      o  888    oo
 * o88o  8  o88o  88ooo88  o888ooo88    888oo88   o888ooooo88 o888ooo8888
 *
 */

static void on_ts(module_data_t *mod, const uint8_t *ts)
{
    if(mod->ca->ca_ready)
        ca_on_ts(mod->ca, ts);

    memcpy(&mod->enc_buffer[mod->enc_buffer_skip], ts, TS_PACKET_SIZE);
    mod->enc_buffer_skip += TS_PACKET_SIZE;

    if(mod->enc_buffer_skip >= BUFFER_SIZE)
    {
        write(mod->sec_fd, mod->enc_buffer, BUFFER_SIZE);
        mod->enc_buffer_skip = 0;
    }
}

static void join_pid(module_data_t *mod, uint16_t pid)
{
    module_stream_demux_join_pid(mod, pid);
}

static void leave_pid(module_data_t *mod, uint16_t pid)
{
    module_stream_demux_leave_pid(mod, pid);
}

static int method_ca_set_pnr(module_data_t *mod)
{
    if(!mod->ca || !mod->ca->ca_fd)
        return 0;

    const uint16_t pnr = lua_tonumber(lua, 2);
    const bool is_set = lua_toboolean(lua, 3);
    ((is_set) ? ca_append_pnr : ca_remove_pnr)(mod->ca, pnr);
    return 0;
}

static void module_init(module_data_t *mod)
{
    module_stream_init(mod, on_ts);
    module_stream_demux_set(mod, join_pid, leave_pid);

    mod->ca = calloc(1, sizeof(dvb_ca_t));

    static const char __adapter[] = "adapter";
    if(!module_option_number(__adapter, &mod->adapter))
    {
        asc_log_error(MSG("option '%s' is required"), __adapter);
        astra_abort();
    }
    module_option_number("device", &mod->device);
    mod->ca->adapter = mod->adapter;
    mod->ca->device = mod->device;

    asc_thread_init(&mod->thread, dvb_thread_loop, mod);
    sec_open(mod);

    struct timespec ts = { .tv_sec = 0, .tv_nsec = 500000 };
    while(!mod->thread_ready)
        nanosleep(&ts, NULL);
}

static void module_destroy(module_data_t *mod)
{
    module_stream_destroy(mod);

    sec_close(mod);
    asc_thread_destroy(&mod->thread);

    free(mod->ca);
}

MODULE_STREAM_METHODS()
MODULE_LUA_METHODS()
{
    { "ca_set_pnr", method_ca_set_pnr },
    MODULE_STREAM_METHODS_REF()
};
MODULE_LUA_REGISTER(dvbcam)
