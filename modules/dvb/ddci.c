/*
 * Astra Module: DigitalDevices standalone CI
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

#include "dvb.h"
#include "src/ca.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>

#define MSG(_msg) "[ddci %d:%d] " _msg, mod->adapter, mod->device

#define BUFFER_SIZE (1022 * TS_PACKET_SIZE)

struct module_data_t
{
    MODULE_STREAM_DATA();

    int adapter;
    int device;

    /* Base */
    char dev_name[32];
    // bool is_thread;

    /* */
    dvb_ca_t *ca;

    /* */
    int enc_sec_fd;

    /* */
    int dec_sec_fd;

    asc_thread_t *sec_thread;
    asc_thread_buffer_t *sec_thread_output;

    bool is_ca_thread_started;
    asc_thread_t *ca_thread;
};

/*
 *  oooooooo8 ooooooooooo  oooooooo8
 * 888         888    88 o888     88
 *  888oooooo  888ooo8   888
 *         888 888    oo 888o     oo
 * o88oooo888 o888ooo8888 888oooo88
 *
 */

static void on_thread_close(void *arg)
{
    module_data_t *mod = arg;

    if(mod->dec_sec_fd > 0)
    {
        close(mod->dec_sec_fd);
        mod->dec_sec_fd = 0;
    }

    if(mod->sec_thread)
    {
        asc_thread_destroy(mod->sec_thread);
        mod->sec_thread = NULL;
    }

    if(mod->sec_thread_output)
    {
        asc_thread_buffer_destroy(mod->sec_thread_output);
        mod->sec_thread_output = NULL;
    }
}

static void on_thread_read(void *arg)
{
    module_data_t *mod = arg;

    uint8_t ts[TS_PACKET_SIZE];
    const ssize_t r = asc_thread_buffer_read(mod->sec_thread_output, ts, sizeof(ts));
    if(r == sizeof(ts))
        module_stream_send(mod, ts);
}

static void thread_loop(void *arg)
{
    module_data_t *mod = arg;
    uint8_t ts[TS_PACKET_SIZE];

    mod->dec_sec_fd = open(mod->dev_name, O_RDONLY);

    while(1)
    {
        const ssize_t len = read(mod->dec_sec_fd, ts, sizeof(ts));
        if(len == -1)
            break;

        if(len == sizeof(ts) && ts[0] == 0x47)
        {
            const ssize_t r = asc_thread_buffer_write(mod->sec_thread_output, ts, sizeof(ts));
            if(r != TS_PACKET_SIZE)
            {
                // overflow
            }
        }
    }
}

static void sec_open(module_data_t *mod)
{
    mod->enc_sec_fd = open(mod->dev_name, O_WRONLY | O_NONBLOCK);
    if(mod->enc_sec_fd <= 0)
    {
        asc_log_error(MSG("failed to open sec [%s]"), strerror(errno));
        astra_abort();
    }

    mod->sec_thread = asc_thread_init(mod);
    mod->sec_thread_output = asc_thread_buffer_init(BUFFER_SIZE);
    asc_thread_start(  mod->sec_thread
                     , thread_loop
                     , on_thread_read, mod->sec_thread_output
                     , on_thread_close);
}

static void sec_close(module_data_t *mod)
{
    if(mod->enc_sec_fd > 0)
    {
        close(mod->enc_sec_fd);
        mod->enc_sec_fd = 0;
    }

    if(mod->sec_thread)
        on_thread_close(mod);
}

/*
 * ooooooooooo ooooo ooooo oooooooooo  ooooooooooo      o      ooooooooo
 * 88  888  88  888   888   888    888  888    88      888      888    88o
 *     888      888ooo888   888oooo88   888ooo8       8  88     888    888
 *     888      888   888   888  88o    888    oo    8oooo88    888    888
 *    o888o    o888o o888o o888o  88o8 o888ooo8888 o88o  o888o o888ooo88
 *
 */

static void on_ca_thread_close(void *arg)
{
    module_data_t *mod = arg;

    mod->is_ca_thread_started = false;

    if(mod->ca_thread)
    {
        asc_thread_destroy(mod->ca_thread);
        mod->ca_thread = NULL;
    }
}

static void ca_thread_loop(void *arg)
{
    module_data_t *mod = arg;

    ca_open(mod->ca);

    nfds_t nfds = 0;

    struct pollfd fds[1];
    memset(fds, 0, sizeof(fds));

    fds[nfds].fd = mod->ca->ca_fd;
    fds[nfds].events = POLLIN;
    ++nfds;

    mod->is_ca_thread_started = true;

    uint64_t current_time = asc_utime();
    uint64_t ca_check_timeout = current_time;

#define CA_TIMEOUT (1 * 1000 * 1000)

    while(mod->is_ca_thread_started)
    {
        const int ret = poll(fds, nfds, 100);
        if(ret > 0)
        {
            if(fds[0].revents)
                ca_loop(mod->ca, fds[0].revents & (POLLPRI | POLLIN));
        }
        else if(ret == 0)
        {
            if((current_time - ca_check_timeout) >= CA_TIMEOUT)
            {
                ca_check_timeout = current_time;
                ca_loop(mod->ca, 0);
            }
        }
        else
        {
            asc_log_error(MSG("poll() failed [%s]"), strerror(errno));
            astra_abort();
        }
    }

#undef CA_TIMEOUT

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
    if(mod->ca->ca_fd > 0)
        ca_on_ts(mod->ca, ts);

    if(write(mod->enc_sec_fd, ts, TS_PACKET_SIZE) != TS_PACKET_SIZE)
        asc_log_error(MSG("sec write failed"));
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

static int module_call(module_data_t *mod)
{
    __uarg(mod);
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
    sprintf(mod->dev_name, "/dev/dvb/adapter%d/sec%d", mod->adapter, mod->device);

    mod->ca_thread = asc_thread_init(mod);
    asc_thread_start(  mod->ca_thread
                     , ca_thread_loop
                     , NULL, NULL
                     , on_ca_thread_close);

    sec_open(mod);

    struct timespec ts = { .tv_sec = 0, .tv_nsec = 500000 };
    while(!mod->is_ca_thread_started)
        nanosleep(&ts, NULL);
}

static void module_destroy(module_data_t *mod)
{
    module_stream_destroy(mod);

    sec_close(mod);

    if(mod->ca_thread)
        on_ca_thread_close(mod);

    free(mod->ca);
}

MODULE_STREAM_METHODS()
MODULE_LUA_METHODS()
{
    { "ca_set_pnr", method_ca_set_pnr },
    MODULE_STREAM_METHODS_REF()
};
MODULE_LUA_REGISTER(ddci)
