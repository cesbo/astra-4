/*
 * Astra Module: DigitalDevices standalone CI
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
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
    MODULE_LUA_DATA();
    MODULE_STREAM_DATA();

    int adapter;
    int device;

    /* Base */
    char dev_name[32];
    asc_thread_t *thread;
    bool thread_ready;

    /* */
    dvb_ca_t *ca;

    /* */
    int enc_sec_fd;

    /* */
    int dec_sec_fd;
    struct
    {
        asc_thread_t *thread;

        int fd[2];
        asc_event_t *event;

        uint8_t *buffer;
        uint32_t buffer_size;
        uint32_t buffer_count;
        uint32_t buffer_read;
        uint32_t buffer_write;
        uint32_t buffer_overflow;
    } sync;
};

/*
 *  oooooooo8 ooooooooooo  oooooooo8
 * 888         888    88 o888     88
 *  888oooooo  888ooo8   888
 *         888 888    oo 888o     oo
 * o88oooo888 o888ooo8888 888oooo88
 *
 */


static void sync_queue_push(module_data_t *mod, const uint8_t *ts)
{
    if(mod->sync.buffer_count >= mod->sync.buffer_size)
    {
        ++mod->sync.buffer_overflow;
        return;
    }

    if(mod->sync.buffer_overflow)
    {
        asc_log_error(MSG("sync buffer overflow. dropped %d packets"), mod->sync.buffer_overflow);
        mod->sync.buffer_overflow = 0;
    }

    memcpy(&mod->sync.buffer[mod->sync.buffer_write], ts, TS_PACKET_SIZE);
    mod->sync.buffer_write += TS_PACKET_SIZE;
    if(mod->sync.buffer_write >= mod->sync.buffer_size)
        mod->sync.buffer_write = 0;

    __sync_fetch_and_add(&mod->sync.buffer_count, TS_PACKET_SIZE);

    uint8_t cmd[1] = { 0 };
    if(send(mod->sync.fd[0], cmd, sizeof(cmd), 0) != sizeof(cmd))
        asc_log_error(MSG("failed to push signal to queue\n"));
}

static void sync_queue_pop(module_data_t *mod, uint8_t *ts)
{
    uint8_t cmd[1];
    if(recv(mod->sync.fd[1], cmd, sizeof(cmd), 0) != sizeof(cmd))
        asc_log_error(MSG("failed to pop signal from queue\n"));

    memcpy(ts, &mod->sync.buffer[mod->sync.buffer_read], TS_PACKET_SIZE);
    mod->sync.buffer_read += TS_PACKET_SIZE;
    if(mod->sync.buffer_read >= mod->sync.buffer_size)
        mod->sync.buffer_read = 0;

    __sync_fetch_and_sub(&mod->sync.buffer_count, TS_PACKET_SIZE);
}

static void sec_thread_loop(void *arg)
{
    module_data_t *mod = arg;
    uint8_t ts[TS_PACKET_SIZE];

    mod->dec_sec_fd = open(mod->dev_name, O_RDONLY);
    asc_thread_while(mod->sync.thread)
    {
        const ssize_t len = read(mod->dec_sec_fd, ts, sizeof(ts));
        if(len == sizeof(ts) && ts[0] == 0x47)
            sync_queue_push(mod, ts);
    }
    if(mod->dec_sec_fd > 0)
        close(mod->dec_sec_fd);
}

static void on_thread_read(void *arg)
{
    module_data_t *mod = arg;

    uint8_t ts[TS_PACKET_SIZE];
    sync_queue_pop(mod, ts);
    module_stream_send(mod, ts);
}

static void sec_open(module_data_t *mod)
{
    mod->enc_sec_fd = open(mod->dev_name, O_WRONLY | O_NONBLOCK);
    if(mod->enc_sec_fd <= 0)
    {
        asc_log_error(MSG("failed to open sec [%s]"), strerror(errno));
        astra_abort();
    }

    socketpair(AF_LOCAL, SOCK_STREAM, 0, mod->sync.fd);

    mod->sync.event = asc_event_init(mod->sync.fd[1], mod);
    asc_event_set_on_read(mod->sync.event, on_thread_read);
    mod->sync.buffer = malloc(BUFFER_SIZE);
    mod->sync.buffer_size = BUFFER_SIZE;

    asc_thread_init(&mod->sync.thread, sec_thread_loop, mod);
}

static void sec_close(module_data_t *mod)
{
    if(mod->enc_sec_fd > 0)
    {
        close(mod->enc_sec_fd);
        mod->enc_sec_fd = 0;
    }

    asc_thread_destroy(&mod->sync.thread);
    asc_event_close(mod->sync.event);
    if(mod->sync.fd[0])
    {
        close(mod->sync.fd[0]);
        close(mod->sync.fd[1]);
    }
    if(mod->sync.buffer)
        free(mod->sync.buffer);
}

/*
 * ooooooooooo ooooo ooooo oooooooooo  ooooooooooo      o      ooooooooo
 * 88  888  88  888   888   888    888  888    88      888      888    88o
 *     888      888ooo888   888oooo88   888ooo8       8  88     888    888
 *     888      888   888   888  88o    888    oo    8oooo88    888    888
 *    o888o    o888o o888o o888o  88o8 o888ooo8888 o88o  o888o o888ooo88
 *
 */

static void ca_thread_loop(void *arg)
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

    asc_thread_init(&mod->thread, ca_thread_loop, mod);
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
MODULE_LUA_REGISTER(ddci)
