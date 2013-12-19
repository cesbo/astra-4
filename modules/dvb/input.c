/*
 * Astra Module: DVB
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

#include "src/fe.h"
#include "src/ca.h"

#include <fcntl.h>
#include <poll.h> // in dvb_thread_loop

#define MSG(_msg) "[dvb_input %d:%d] " _msg, mod->adapter, mod->device

#define DVB_API ((DVB_API_VERSION * 100) + DVB_API_VERSION_MINOR)
#define DVR_BUFFER_SIZE (1022 * TS_PACKET_SIZE)

struct module_data_t
{
    MODULE_LUA_DATA();
    MODULE_STREAM_DATA();

    int adapter;
    int device;

    /* Base */
    asc_thread_t *thread;
    bool thread_ready;

    /* DVR Config */
    int dvr_buffer_size;

    /* DVR Base */
    int dvr_fd;
    asc_event_t *dvr_event;
    uint8_t dvr_buffer[DVR_BUFFER_SIZE];

    uint32_t dvr_read;

    /* DMX config */
    int dmx_budget;

    /* DMX Base */
    char dmx_dev_name[32];
    int *dmx_fd_list;

    dvb_fe_t *fe;
    dvb_ca_t *ca;
};

/*
 * ooooooooo  ooooo  oooo oooooooooo
 *  888    88o 888    88   888    888
 *  888    888  888  88    888oooo88
 *  888    888   88888     888  88o
 * o888ooo88      888     o888o  88o8
 *
 */

static void dvr_open(module_data_t *mod);
static void dvr_close(module_data_t *mod);

static void dvr_on_error(void *arg)
{
    module_data_t *mod = arg;
    asc_log_error(MSG("dvr read error, try to reopen [%s]"), strerror(errno));
    dvr_close(mod);
    dvr_open(mod);
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
        if(mod->ca->ca_fd > 0)
            ca_on_ts(mod->ca, &mod->dvr_buffer[i]);

        module_stream_send(mod, &mod->dvr_buffer[i]);
    }
}

static void dvr_open(module_data_t *mod)
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

static void dvr_close(module_data_t *mod)
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

/*
 * ooooooooo  ooooooooooo oooo     oooo ooooo  oooo ooooo  oooo
 *  888    88o 888    88   8888o   888   888    88    888  88
 *  888    888 888ooo8     88 888o8 88   888    88      888
 *  888    888 888    oo   88  888  88   888    88     88 888
 * o888ooo88  o888ooo8888 o88o  8  o88o   888oo88   o88o  o888o
 *
 */

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

/*
 *   ooooooo  oooooooooo  ooooooooooo ooooo  ooooooo  oooo   oooo oooooooo8
 * o888   888o 888    888 88  888  88  888 o888   888o 8888o  88 888
 * 888     888 888oooo88      888      888 888     888 88 888o88  888oooooo
 * 888o   o888 888            888      888 888o   o888 88   8888         888
 *   88ooo88  o888o          o888o    o888o  88ooo88  o88o    88 o88oooo888
 *
 */

static void option_required(module_data_t *mod, const char *name)
{
    asc_log_error(MSG("option '%s' is required"), name);
    astra_abort();
}

static void option_unknown_type(module_data_t *mod, const char *name, const char *value)
{
    asc_log_error(MSG("unknown type of the '%s': %s"), name, value);
    astra_abort();
}

static void module_option_fec(module_data_t *mod)
{
    const char *string_val;
    static const char __fec[] = "fec";
    if(module_option_string(__fec, &string_val, NULL))
    {
        if(!strcasecmp(string_val, "NONE")) mod->fe->fec = FEC_NONE;
        else if(!strcasecmp(string_val, "1/2")) mod->fe->fec = FEC_1_2;
        else if(!strcasecmp(string_val, "2/3")) mod->fe->fec = FEC_2_3;
        else if(!strcasecmp(string_val, "3/4")) mod->fe->fec = FEC_3_4;
        else if(!strcasecmp(string_val, "4/5")) mod->fe->fec = FEC_4_5;
        else if(!strcasecmp(string_val, "5/6")) mod->fe->fec = FEC_5_6;
        else if(!strcasecmp(string_val, "6/7")) mod->fe->fec = FEC_6_7;
        else if(!strcasecmp(string_val, "7/8")) mod->fe->fec = FEC_7_8;
        else if(!strcasecmp(string_val, "8/9")) mod->fe->fec = FEC_8_9;
#if DVB_API_VERSION >= 5
        else if(!strcasecmp(string_val, "3/5")) mod->fe->fec = FEC_3_5;
        else if(!strcasecmp(string_val, "9/10")) mod->fe->fec = FEC_9_10;
#endif
        else
            option_unknown_type(mod, __fec, string_val);
    }
    else
        mod->fe->fec = FEC_AUTO;
}

/*
 * ooooooooo  ooooo  oooo oooooooooo           oooooooo8
 *  888    88o 888    88   888    888         888
 *  888    888  888  88    888oooo88 ooooooooo 888oooooo
 *  888    888   88888     888    888                 888
 * o888ooo88      888     o888ooo888          o88oooo888
 *
 */

static void module_options_s(module_data_t *mod)
{
    const char *string_val;

    /* Transponder options */
    mod->fe->frequency *= 1000;

    static const char __polarization[] = "polarization";
    if(!module_option_string(__polarization, &string_val, NULL))
        option_required(mod, __polarization);

    const char pol = (string_val[0] > 'Z') ? (string_val[0] - ('z' - 'Z')) : string_val[0];
    if(pol == 'V' || pol == 'R')
        mod->fe->polarization = SEC_VOLTAGE_13;
    else if(pol == 'H' || pol == 'L')
        mod->fe->polarization = SEC_VOLTAGE_18;

    static const char __symbolrate[] = "symbolrate";
    if(!module_option_number(__symbolrate, &mod->fe->symbolrate))
        option_required(mod, __symbolrate);
    mod->fe->symbolrate *= 1000;

    /* LNB options */
    static const char __lof1[] = "lof1";
    if(!module_option_number(__lof1, &mod->fe->lnb_lof1))
        option_required(mod, __lof1);
    module_option_number("lof2", &mod->fe->lnb_lof2);
    module_option_number("slof", &mod->fe->lnb_slof);

    mod->fe->lnb_lof1 *= 1000;
    mod->fe->lnb_lof2 *= 1000;
    mod->fe->lnb_slof *= 1000;

    module_option_boolean("lnb_sharing", &mod->fe->lnb_sharing);
    module_option_number("diseqc", &mod->fe->diseqc);
    module_option_boolean("tone", &mod->fe->force_tone);

    static const char __rolloff[] = "rolloff";
    if(module_option_string(__rolloff, &string_val, NULL))
    {
        if(!strcasecmp(string_val, "AUTO")) mod->fe->rolloff = ROLLOFF_AUTO;
        else if(!strcasecmp(string_val, "35")) mod->fe->rolloff = ROLLOFF_35;
        else if(!strcasecmp(string_val, "20")) mod->fe->rolloff = ROLLOFF_20;
        else if(!strcasecmp(string_val, "25")) mod->fe->rolloff = ROLLOFF_25;
        else
            option_unknown_type(mod, __rolloff, string_val);
    }
    else
        mod->fe->rolloff = ROLLOFF_35;

    module_option_fec(mod);
    module_option_number("stream_id", &mod->fe->stream_id);
}

/*
 * ooooooooo  ooooo  oooo oooooooooo       ooooooooooo
 *  888    88o 888    88   888    888      88  888  88
 *  888    888  888  88    888oooo88 ooooooooo 888
 *  888    888   88888     888    888          888
 * o888ooo88      888     o888ooo888          o888o
 *
 */

static void module_options_t(module_data_t *mod)
{
    const char *string_val;

    mod->fe->frequency *= 1000000;

    static const char __bandwidth[] = "bandwidth";
    if(module_option_string(__bandwidth, &string_val, NULL))
    {
        if(!strcasecmp(string_val, "AUTO")) mod->fe->bandwidth = BANDWIDTH_AUTO;
        else if(!strcasecmp(string_val, "8MHZ")) mod->fe->bandwidth = BANDWIDTH_8_MHZ;
        else if(!strcasecmp(string_val, "7MHZ")) mod->fe->bandwidth = BANDWIDTH_7_MHZ;
        else if(!strcasecmp(string_val, "6MHZ")) mod->fe->bandwidth = BANDWIDTH_6_MHZ;
        else
            option_unknown_type(mod, __bandwidth, string_val);
    }
    else
        mod->fe->bandwidth = BANDWIDTH_AUTO;

    static const char __guardinterval[] = "guardinterval";
    if(module_option_string(__guardinterval, &string_val, NULL))
    {
        if(!strcasecmp(string_val, "AUTO")) mod->fe->guardinterval = GUARD_INTERVAL_AUTO;
        else if(!strcasecmp(string_val, "1/32")) mod->fe->guardinterval = GUARD_INTERVAL_1_32;
        else if(!strcasecmp(string_val, "1/16")) mod->fe->guardinterval = GUARD_INTERVAL_1_16;
        else if(!strcasecmp(string_val, "1/8")) mod->fe->guardinterval = GUARD_INTERVAL_1_8;
        else if(!strcasestr(string_val, "1/4")) mod->fe->guardinterval = GUARD_INTERVAL_1_4;
        else
            option_unknown_type(mod, __guardinterval, string_val);
    }
    else
        mod->fe->guardinterval = GUARD_INTERVAL_AUTO;

    static const char __transmitmode[] = "transmitmode";
    if(module_option_string(__transmitmode, &string_val, NULL))
    {
        if(!strcasecmp(string_val, "AUTO")) mod->fe->transmitmode = TRANSMISSION_MODE_AUTO;
        else if(!strcasecmp(string_val, "2K")) mod->fe->transmitmode = TRANSMISSION_MODE_2K;
        else if(!strcasecmp(string_val, "8K")) mod->fe->transmitmode = TRANSMISSION_MODE_8K;
        else if(!strcasecmp(string_val, "4K")) mod->fe->transmitmode = TRANSMISSION_MODE_4K;
        else
            option_unknown_type(mod, __transmitmode, string_val);
    }
    else
        mod->fe->transmitmode = TRANSMISSION_MODE_AUTO;

    static const char __hierarchy[] = "hierarchy";
    if(module_option_string(__hierarchy, &string_val, NULL))
    {
        if(!strcasecmp(string_val, "AUTO")) mod->fe->hierarchy = HIERARCHY_AUTO;
        else if(!strcasecmp(string_val, "NONE")) mod->fe->hierarchy = HIERARCHY_NONE;
        else if(!strcasecmp(string_val, "1")) mod->fe->hierarchy = HIERARCHY_1;
        else if(!strcasecmp(string_val, "2")) mod->fe->hierarchy = HIERARCHY_2;
        else if(!strcasecmp(string_val, "4")) mod->fe->hierarchy = HIERARCHY_4;
        else
            option_unknown_type(mod, __hierarchy, string_val);
    }
    else
        mod->fe->hierarchy = HIERARCHY_AUTO;

    module_option_number("stream_id", &mod->fe->stream_id);
}

/*
 * ooooooooo  ooooo  oooo oooooooooo             oooooooo8
 *  888    88o 888    88   888    888          o888     88
 *  888    888  888  88    888oooo88 ooooooooo 888
 *  888    888   88888     888    888          888o     oo
 * o888ooo88      888     o888ooo888            888oooo88
 *
 */


static void module_options_c(module_data_t *mod)
{
    mod->fe->frequency *= 1000000;

    static const char __symbolrate[] = "symbolrate";
    if(!module_option_number(__symbolrate, &mod->fe->symbolrate))
        option_required(mod, __symbolrate);
    mod->fe->symbolrate *= 1000;

    module_option_fec(mod);
}

/*
 * oooooooooo      o       oooooooo8 ooooooooooo
 *  888    888    888     888         888    88
 *  888oooo88    8  88     888oooooo  888ooo8
 *  888    888  8oooo88           888 888    oo
 * o888ooo888 o88o  o888o o88oooo888 o888ooo8888
 *
 */

static void module_options(module_data_t *mod)
{
    static const char __adapter[] = "adapter";
    if(!module_option_number(__adapter, &mod->adapter))
        option_required(mod, __adapter);
    module_option_number("device", &mod->device);

    mod->fe->adapter = mod->adapter;
    mod->ca->adapter = mod->adapter;
    mod->fe->device = mod->device;
    mod->ca->device = mod->device;

    const char *string_val = NULL;

    static const char __type[] = "type";
    if(!module_option_string(__type, &string_val, NULL))
        option_required(mod, __type);

    if(!strcasecmp(string_val, "S")) mod->fe->type = DVB_TYPE_S;
    else if(!strcasecmp(string_val, "T")) mod->fe->type = DVB_TYPE_T;
    else if(!strcasecmp(string_val, "C")) mod->fe->type = DVB_TYPE_C;
#if DVB_API >= 500
    else if(!strcasecmp(string_val, "S2")) mod->fe->type = DVB_TYPE_S2;
#if DVB_API >= 503
    else if(!strcasecmp(string_val, "T2")) mod->fe->type = DVB_TYPE_T2;
#endif
#endif
    else
        option_unknown_type(mod, __adapter, string_val);

    static const char __frequency[] = "frequency";
    if(!module_option_number(__frequency, &mod->fe->frequency))
        option_required(mod, __frequency);

    module_option_boolean("budget", &mod->dmx_budget);
    module_option_number("buffer_size", &mod->dvr_buffer_size);

    static const char __modulation[] = "modulation";
    if(module_option_string(__modulation, &string_val, NULL))
    {
        if(!strcasecmp(string_val, "NONE")) mod->fe->modulation = FE_MODULATION_NONE;
        else if(!strcasecmp(string_val, "QPSK")) mod->fe->modulation = QPSK;
        else if(!strcasecmp(string_val, "QAM16")) mod->fe->modulation = QAM_16;
        else if(!strcasecmp(string_val, "QAM32")) mod->fe->modulation = QAM_32;
        else if(!strcasecmp(string_val, "QAM64")) mod->fe->modulation = QAM_64;
        else if(!strcasecmp(string_val, "QAM128")) mod->fe->modulation = QAM_128;
        else if(!strcasecmp(string_val, "QAM256")) mod->fe->modulation = QAM_256;
        else if(!strcasecmp(string_val, "AUTO")) mod->fe->modulation = QAM_AUTO;
        else if(!strcasecmp(string_val, "VSB8")) mod->fe->modulation = VSB_8;
        else if(!strcasecmp(string_val, "VSB16")) mod->fe->modulation = VSB_16;
        else if(!strcasecmp(string_val, "PSK8")) mod->fe->modulation = PSK_8;
        else if(!strcasecmp(string_val, "APSK16")) mod->fe->modulation = APSK_16;
        else if(!strcasecmp(string_val, "APSK32")) mod->fe->modulation = APSK_32;
        else if(!strcasecmp(string_val, "DQPSK")) mod->fe->modulation = DQPSK;
        else
            option_unknown_type(mod, __modulation, string_val);
    }
    else
        mod->fe->modulation = FE_MODULATION_NONE;

    switch(mod->fe->type)
    {
        case DVB_TYPE_S:
        case DVB_TYPE_S2:
            module_options_s(mod);
            break;
        case DVB_TYPE_T:
        case DVB_TYPE_T2:
            module_options_t(mod);
            break;
        case DVB_TYPE_C:
            module_options_c(mod);
            break;
        default:
            break;
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

    fe_open(mod->fe);
    ca_open(mod->ca);
    dmx_open(mod);

    nfds_t nfds = 0;

    struct pollfd fds[2];
    memset(fds, 0, sizeof(fds));

    fds[nfds].fd = mod->fe->fe_fd;
    fds[nfds].events = POLLIN;
    ++nfds;

    if(mod->ca->ca_fd)
    {
        fds[nfds].fd = mod->ca->ca_fd;
        fds[nfds].events = POLLIN;
        ++nfds;
    }

    mod->thread_ready = true;

    uint64_t current_time = asc_utime();
    uint64_t dmx_check_timeout = current_time;
    uint64_t ca_check_timeout = current_time;

#define DMX_TIMEOUT (200 * 1000)
#define CA_TIMEOUT (1 * 1000 * 1000)

    asc_thread_while(mod->thread)
    {
        const int ret = poll(fds, nfds, 100);

        if(ret < 0)
        {
            asc_log_error(MSG("poll() failed [%s]"), strerror(errno));
            astra_abort();
        }

        if(ret > 0)
        {
            if(fds[0].revents)
                fe_loop(mod->fe, fds[0].revents & (POLLPRI | POLLIN));
            if(mod->ca->ca_fd && fds[1].revents)
                ca_loop(mod->ca, fds[1].revents & (POLLPRI | POLLIN));
        }

        current_time = asc_utime();
        if(!mod->dmx_budget && (current_time - dmx_check_timeout) >= DMX_TIMEOUT)
        {
            dmx_check_timeout = current_time;

            for(int i = 0; i < MAX_PID; ++i)
            {
                if((mod->__stream.pid_list[i] > 0) && (mod->dmx_fd_list[i] == 0))
                    dmx_set_pid(mod, i, 1);
                else if((mod->__stream.pid_list[i] == 0) && (mod->dmx_fd_list[i] > 0))
                    dmx_set_pid(mod, i, 0);
            }
        }

        if((current_time - ca_check_timeout) >= CA_TIMEOUT)
        {
            ca_check_timeout = current_time;
            fe_loop(mod->fe, 0);
            if(mod->ca->ca_fd)
                ca_loop(mod->ca, 0);
        }
    }

#undef DMX_TIMEOUT
#undef CA_TIMEOUT

    fe_close(mod->fe);
    ca_close(mod->ca);
    dmx_close(mod);
}

/*
 * oooo     oooo  ooooooo  ooooooooo  ooooo  oooo ooooo       ooooooooooo
 *  8888o   888 o888   888o 888    88o 888    88   888         888    88
 *  88 888o8 88 888     888 888    888 888    88   888         888ooo8
 *  88  888  88 888o   o888 888    888 888    88   888      o  888    oo
 * o88o  8  o88o  88ooo88  o888ooo88    888oo88   o888ooooo88 o888ooo8888
 *
 */

static int method_ca_set_pnr(module_data_t *mod)
{
    if(!mod->ca || !mod->ca->ca_fd)
        return 0;

    const uint16_t pnr = lua_tonumber(lua, 2);
    const bool is_set = lua_toboolean(lua, 3);
    ((is_set) ? ca_append_pnr : ca_remove_pnr)(mod->ca, pnr);
    return 0;
}

static void join_pid(module_data_t *mod, uint16_t pid)
{
    ++mod->__stream.pid_list[pid];
}

static void leave_pid(module_data_t *mod, uint16_t pid)
{
    --mod->__stream.pid_list[pid];
}

static void module_init(module_data_t *mod)
{
    module_stream_init(mod, NULL);
    module_stream_demux_set(mod, join_pid, leave_pid);

    mod->fe = calloc(1, sizeof(dvb_fe_t));
    mod->ca = calloc(1, sizeof(dvb_ca_t));

    module_options(mod);

    asc_thread_init(&mod->thread, dvb_thread_loop, mod);
    dvr_open(mod);

    struct timespec ts = { .tv_sec = 0, .tv_nsec = 500000 };
    while(!mod->thread_ready)
        nanosleep(&ts, NULL);
}

static void module_destroy(module_data_t *mod)
{
    module_stream_destroy(mod);

    dvr_close(mod);
    asc_thread_destroy(&mod->thread);

    free(mod->fe);
    free(mod->ca);
}

MODULE_STREAM_METHODS()
MODULE_LUA_METHODS()
{
    { "ca_set_pnr", method_ca_set_pnr },
    MODULE_STREAM_METHODS_REF()
};
MODULE_LUA_REGISTER(dvb_input)
