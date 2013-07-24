/*
 * Astra DVB Module
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#include "dvb.h"

#include <poll.h> // in dvb_thread_loop

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
    if(module_option_string(__fec, &string_val))
    {
        if(!strcasecmp(string_val, "NONE")) mod->fec = FEC_NONE;
        else if(!strcasecmp(string_val, "1/2")) mod->fec = FEC_1_2;
        else if(!strcasecmp(string_val, "2/3")) mod->fec = FEC_2_3;
        else if(!strcasecmp(string_val, "3/4")) mod->fec = FEC_3_4;
        else if(!strcasecmp(string_val, "4/5")) mod->fec = FEC_4_5;
        else if(!strcasecmp(string_val, "5/6")) mod->fec = FEC_5_6;
        else if(!strcasecmp(string_val, "6/7")) mod->fec = FEC_6_7;
        else if(!strcasecmp(string_val, "7/8")) mod->fec = FEC_7_8;
        else if(!strcasecmp(string_val, "8/9")) mod->fec = FEC_8_9;
#if DVB_API_VERSION >= 5
        else if(!strcasecmp(string_val, "3/5")) mod->fec = FEC_3_5;
        else if(!strcasecmp(string_val, "9/10")) mod->fec = FEC_9_10;
#endif
        else
            option_unknown_type(mod, __fec, string_val);
    }
    else
        mod->fec = FEC_AUTO;
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
    mod->frequency *= 1000;

    static const char __polarization[] = "polarization";
    if(!module_option_string(__polarization, &string_val))
        option_required(mod, __polarization);

    const char pol = (string_val[0] > 'Z') ? (string_val[0] - ('z' - 'Z')) : string_val[0];
    if(pol == 'V' || pol == 'R')
        mod->polarization = SEC_VOLTAGE_13;
    else if(pol == 'H' || pol == 'L')
        mod->polarization = SEC_VOLTAGE_18;

    static const char __symbolrate[] = "symbolrate";
    if(!module_option_number(__symbolrate, &mod->symbolrate))
        option_required(mod, __symbolrate);
    mod->symbolrate *= 1000;

    /* LNB options */
    static const char __lof1[] = "lof1";
    if(!module_option_number(__lof1, &mod->lnb_lof1))
        option_required(mod, __lof1);
    module_option_number("lof2", &mod->lnb_lof2);
    module_option_number("slof", &mod->lnb_slof);

    mod->lnb_lof1 *= 1000;
    mod->lnb_lof2 *= 1000;
    mod->lnb_slof *= 1000;

    module_option_number("lnb_sharing", &mod->lnb_sharing);
    module_option_number("diseqc", &mod->diseqc);
    module_option_number("tone", &mod->force_tone);

    static const char __rolloff[] = "rolloff";
    if(module_option_string(__rolloff, &string_val))
    {
        if(!strcasecmp(string_val, "AUTO")) mod->rolloff = ROLLOFF_AUTO;
        else if(!strcasecmp(string_val, "35")) mod->rolloff = ROLLOFF_35;
        else if(!strcasecmp(string_val, "20")) mod->rolloff = ROLLOFF_20;
        else if(!strcasecmp(string_val, "25")) mod->rolloff = ROLLOFF_25;
        else
            option_unknown_type(mod, __rolloff, string_val);
    }
    else
        mod->rolloff = ROLLOFF_35;

    module_option_fec(mod);
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

    mod->frequency *= 1000000;

    static const char __bandwidth[] = "bandwidth";
    if(module_option_string(__bandwidth, &string_val))
    {
        if(!strcasecmp(string_val, "AUTO")) mod->bandwidth = BANDWIDTH_AUTO;
        else if(!strcasecmp(string_val, "8MHZ")) mod->bandwidth = BANDWIDTH_8_MHZ;
        else if(!strcasecmp(string_val, "7MHZ")) mod->bandwidth = BANDWIDTH_7_MHZ;
        else if(!strcasecmp(string_val, "6MHZ")) mod->bandwidth = BANDWIDTH_6_MHZ;
        else
            option_unknown_type(mod, __bandwidth, string_val);
    }
    else
        mod->bandwidth = BANDWIDTH_AUTO;

    static const char __guardinterval[] = "guardinterval";
    if(module_option_string(__guardinterval, &string_val))
    {
        if(!strcasecmp(string_val, "AUTO")) mod->guardinterval = GUARD_INTERVAL_AUTO;
        else if(!strcasecmp(string_val, "1/32")) mod->guardinterval = GUARD_INTERVAL_1_32;
        else if(!strcasecmp(string_val, "1/16")) mod->guardinterval = GUARD_INTERVAL_1_16;
        else if(!strcasecmp(string_val, "1/8")) mod->guardinterval = GUARD_INTERVAL_1_8;
        else if(!strcasestr(string_val, "1/4")) mod->guardinterval = GUARD_INTERVAL_1_4;
        else
            option_unknown_type(mod, __guardinterval, string_val);
    }
    else
        mod->guardinterval = GUARD_INTERVAL_AUTO;

    static const char __transmitmode[] = "transmitmode";
    if(module_option_string(__transmitmode, &string_val))
    {
        if(!strcasecmp(string_val, "AUTO")) mod->transmitmode = TRANSMISSION_MODE_AUTO;
        else if(!strcasecmp(string_val, "2K")) mod->transmitmode = TRANSMISSION_MODE_2K;
        else if(!strcasecmp(string_val, "8K")) mod->transmitmode = TRANSMISSION_MODE_8K;
        else if(!strcasecmp(string_val, "4K")) mod->transmitmode = TRANSMISSION_MODE_4K;
        else
            option_unknown_type(mod, __transmitmode, string_val);
    }
    else
        mod->transmitmode = TRANSMISSION_MODE_AUTO;

    static const char __hierarchy[] = "hierarchy";
    if(module_option_string(__hierarchy, &string_val))
    {
        if(!strcasecmp(string_val, "AUTO")) mod->hierarchy = HIERARCHY_AUTO;
        else if(!strcasecmp(string_val, "NONE")) mod->hierarchy = HIERARCHY_NONE;
        else if(!strcasecmp(string_val, "1")) mod->hierarchy = HIERARCHY_1;
        else if(!strcasecmp(string_val, "2")) mod->hierarchy = HIERARCHY_2;
        else if(!strcasecmp(string_val, "4")) mod->hierarchy = HIERARCHY_4;
        else
            option_unknown_type(mod, __hierarchy, string_val);
    }
    else
        mod->hierarchy = HIERARCHY_AUTO;
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
    mod->frequency *= 1000000;

    static const char __symbolrate[] = "symbolrate";
    if(!module_option_number(__symbolrate, &mod->symbolrate))
        option_required(mod, __symbolrate);
    mod->symbolrate *= 1000;

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

    const char *string_val = NULL;

    static const char __type[] = "type";
    if(!module_option_string(__type, &string_val))
        option_required(mod, __type);

    if(!strcasecmp(string_val, "S")) mod->type = DVB_TYPE_S;
    else if(!strcasecmp(string_val, "T")) mod->type = DVB_TYPE_T;
    else if(!strcasecmp(string_val, "C")) mod->type = DVB_TYPE_C;
#if DVB_API >= 500
    else if(!strcasecmp(string_val, "S2")) mod->type = DVB_TYPE_S2;
#if DVB_API >= 505
    else if(!strcasecmp(string_val, "T2")) mod->type = DVB_TYPE_T2;
#endif
#endif
    else
        option_unknown_type(mod, __adapter, string_val);

    static const char __frequency[] = "frequency";
    if(!module_option_number(__frequency, &mod->frequency))
        option_required(mod, __frequency);

    module_option_number("budget", &mod->dmx_budget);
    module_option_number("buffer_size", &mod->dvr_buffer_size);

    static const char __modulation[] = "modulation";
    if(module_option_string(__modulation, &string_val))
    {
        if(!strcasecmp(string_val, "NONE")) mod->modulation = FE_MODULATION_NONE;
        else if(!strcasecmp(string_val, "QPSK")) mod->modulation = QPSK;
        else if(!strcasecmp(string_val, "QAM16")) mod->modulation = QAM_16;
        else if(!strcasecmp(string_val, "QAM32")) mod->modulation = QAM_32;
        else if(!strcasecmp(string_val, "QAM64")) mod->modulation = QAM_64;
        else if(!strcasecmp(string_val, "QAM128")) mod->modulation = QAM_128;
        else if(!strcasecmp(string_val, "QAM256")) mod->modulation = QAM_256;
        else if(!strcasecmp(string_val, "AUTO")) mod->modulation = QAM_AUTO;
        else if(!strcasecmp(string_val, "VSB8")) mod->modulation = VSB_8;
        else if(!strcasecmp(string_val, "VSB16")) mod->modulation = VSB_16;
        else if(!strcasecmp(string_val, "PSK8")) mod->modulation = PSK_8;
        else if(!strcasecmp(string_val, "APSK16")) mod->modulation = APSK_16;
        else if(!strcasecmp(string_val, "APSK32")) mod->modulation = APSK_32;
        else if(!strcasecmp(string_val, "DQPSK")) mod->modulation = DQPSK;
        else
            option_unknown_type(mod, __modulation, string_val);
    }
    else
        mod->modulation = FE_MODULATION_NONE;

    switch(mod->type)
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

    mod->thread_ready = true;

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
    if(!mod->ca_fd)
        return 0;

    const uint16_t pnr = lua_tonumber(lua, 2);
    const bool is_set = lua_toboolean(lua, 3);
    ((is_set) ? ca_append_pnr : ca_remove_pnr)(mod, pnr);
    return 0;
}

static void join_pid(module_data_t *mod, uint16_t pid)
{
    ++mod->__stream.pid_list[pid];
    if(mod->__stream.pid_list[pid] == 1)
        dmx_set_pid(mod, pid, 1);
}

static void leave_pid(module_data_t *mod, uint16_t pid)
{
    --mod->__stream.pid_list[pid];
    if(mod->__stream.pid_list[pid] == 0)
        dmx_set_pid(mod, pid, 0);
}

static void module_init(module_data_t *mod)
{
    module_stream_init(mod, NULL);
    module_stream_demux_set(mod, join_pid, leave_pid);

    module_options(mod);

    asc_thread_init(&mod->thread, dvb_thread_loop, mod);
    dvr_open(mod);
    dmx_open(mod);

    struct timespec ts = { .tv_sec = 0, .tv_nsec = 500000 };
    while(!mod->thread_ready)
        nanosleep(&ts, NULL);
}

static void module_destroy(module_data_t *mod)
{
    module_stream_destroy(mod);

    dmx_close(mod);
    dvr_close(mod);
    asc_thread_destroy(&mod->thread);
}

MODULE_STREAM_METHODS()
MODULE_LUA_METHODS()
{
    { "ca_set_pnr", method_ca_set_pnr },
    MODULE_STREAM_METHODS_REF()
};
MODULE_LUA_REGISTER(dvb_input)
