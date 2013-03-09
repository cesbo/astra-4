/*
 * Astra DVB Module
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#include "../dvb.h"
#include <fcntl.h>
#include <poll.h>

#if DVB_API_VERSION >= 5

#define DTV_PROPERTY_BEGIN(_cmdseq, _cmdlist)                                                   \
    _cmdseq.num = 0;                                                                            \
    _cmdseq.props = _cmdlist

#define DTV_PROPERTY_SET(_cmdseq, _cmdlist, _cmd, _data)                                        \
    _cmdlist[_cmdseq.num].cmd = _cmd;                                                           \
    _cmdlist[_cmdseq.num].u.data = _data;                                                       \
    ++_cmdseq.num

#endif

static void fe_clear(module_data_t *mod)
{
#if DVB_API_VERSION >= 5
    static struct dtv_property clear[] = { { .cmd = DTV_CLEAR } };
    static struct dtv_properties cmdclear = { .num = 1, .props = clear };
    if(ioctl(mod->fe_fd, FE_SET_PROPERTY, &cmdclear ) != 0)
    {
        asc_log_error(MSG("FE_SET_PROPERTY DTV_CLEAR failed [%s]"), strerror(errno));
        astra_abort();
    }
#endif
    struct dvb_frontend_event fe_event;
    while(ioctl(mod->fe_fd, FE_GET_EVENT, &fe_event) != -1)
        ;
}

/*
 * ooooooooooo ooooo  oooo ooooooooooo oooo   oooo ooooooooooo
 *  888    88   888    88   888    88   8888o  88  88  888  88
 *  888ooo8      888  88    888ooo8     88 888o88      888
 *  888    oo     88888     888    oo   88   8888      888
 * o888ooo8888     888     o888ooo8888 o88o    88     o888o
 *
 */

static void fe_event(module_data_t *mod)
{
    struct dvb_frontend_event dvb_fe_event;
    fe_status_t fe_status, fe_status_diff;

    while(1)
    {
        if(ioctl(mod->fe_fd, FE_GET_EVENT, &dvb_fe_event) != 0)
        {
            if(errno == EWOULDBLOCK)
                return;
            asc_log_error(MSG("FE_GET_EVENT failed [%s]"), strerror(errno));
            return;
        }

        fe_status = dvb_fe_event.status;
        fe_status_diff = fe_status ^ mod->fe_status;
        mod->fe_status = fe_status;

        const char ss = (fe_status & FE_HAS_SIGNAL) ? 'S' : '_';
        const char sc = (fe_status & FE_HAS_CARRIER) ? 'C' : '_';
        const char sv = (fe_status & FE_HAS_VITERBI) ? 'V' : '_';
        const char sy = (fe_status & FE_HAS_SYNC) ? 'Y' : '_';
        const char sl = (fe_status & FE_HAS_LOCK) ? 'L' : '_';

        if(fe_status_diff & FE_HAS_LOCK)
        {
            mod->lock = fe_status & FE_HAS_LOCK;
            if(mod->lock)
            {
                if(ioctl(mod->fe_fd, FE_READ_SIGNAL_STRENGTH, &mod->signal) != 0)
                    mod->signal = -2;
                else
                    mod->signal = (mod->signal * 100) / 0xFFFF;

                if(ioctl(mod->fe_fd, FE_READ_SNR, &mod->snr) != 0)
                    mod->snr = -2;
                else
                    mod->snr = (mod->snr * 100) / 0xFFFF;

                if(ioctl(mod->fe_fd, FE_READ_BER, &mod->ber) != 0)
                    mod->ber = -2;

                if(ioctl(mod->fe_fd, FE_READ_UNCORRECTED_BLOCKS, &mod->unc) != 0)
                    mod->unc = -2;

                asc_log_info(MSG("fe has lock. status:%c%c%c%c%c signal:%d%% snr:%d%%")
                             , ss, sc, sv, sy, sl
                             , mod->signal, mod->snr);
            }
            else
            {
                asc_log_warning(MSG("fe has lost lock. status:%c%c%c%c%c")
                                , ss, sc, sv, sy, sl);
                mod->do_retune = 1;
            }
        }

        if(fe_status_diff & FE_REINIT)
        {
            if(fe_status & FE_REINIT)
            {
                asc_log_warning(MSG("fe was reinitialized"));
                fe_clear(mod);
                mod->do_retune = 1;
            }
        }
    }
}

/*
 *  oooooooo8 ooooooooooo   o   ooooooooooo ooooo  oooo oooooooo8
 * 888        88  888  88  888  88  888  88  888    88 888
 *  888oooooo     888     8  88     888      888    88  888oooooo
 *         888    888    8oooo88    888      888    88         888
 * o88oooo888    o888o o88o  o888o o888o      888oo88  o88oooo888
 *
 */

static void fe_status(module_data_t *mod)
{
    fe_status_t fe_status;
    if(ioctl(mod->fe_fd, FE_READ_STATUS, &fe_status) != 0)
    {
        asc_log_error(MSG("FE_READ_STATUS failed [%s]"), strerror(errno));
        astra_abort();
    }

    mod->lock = fe_status & FE_HAS_LOCK;
    if(!mod->lock)
    {
        mod->do_retune = 1;
        return;
    }

    if(ioctl(mod->fe_fd, FE_READ_SIGNAL_STRENGTH, &mod->signal) != 0)
        mod->signal = -2;
    else
        mod->signal = (mod->signal * 100) / 0xFFFF;

    if(ioctl(mod->fe_fd, FE_READ_SNR, &mod->snr) != 0)
        mod->snr = -2;
    else
        mod->snr = (mod->snr * 100) / 0xFFFF;

    if(ioctl(mod->fe_fd, FE_READ_BER, &mod->ber) != 0)
        mod->ber = -2;

    if(ioctl(mod->fe_fd, FE_READ_UNCORRECTED_BLOCKS, &mod->unc) != 0)
        mod->unc = -2;
}

/*
 * ooooooooo   o88    oooooooo8 ooooooooooo              oooooooo8
 *  888    88o oooo  888         888    88   ooooooooo o888     88
 *  888    888  888   888oooooo  888ooo8   888    888  888
 *  888    888  888          888 888    oo 888    888  888o     oo
 * o888ooo88   o888o o88oooo888 o888ooo8888  88ooo888   888oooo88
 *                                                888o
 */

static void diseqc_setup(module_data_t *mod, int voltage, int tone)
{
    static struct timespec ns = { .tv_sec = 0, .tv_nsec = 15 * 1000 * 1000 };

    if(ioctl(mod->fe_fd, FE_SET_TONE, SEC_TONE_OFF) != 0)
    {
        asc_log_error(MSG("diseqc: FE_SET_TONE failed [%s]"), strerror(errno));
        astra_abort();
    }

    if(ioctl(mod->fe_fd, FE_SET_VOLTAGE, voltage) != 0)
    {
        asc_log_error(MSG("diseqc: FE_SET_VOLTAGE failed [%s]"), strerror(errno));
        astra_abort();
    }

    nanosleep(&ns, NULL);

    const int data0 = 0xF0
                    | ((mod->diseqc - 1) << 2)
                    | ((voltage == SEC_VOLTAGE_18) << 1)
                    | (tone == SEC_TONE_ON);

    struct dvb_diseqc_master_cmd cmd =
    {
        .msg = { 0xE0, 0x10, 0x38, data0, 0x00, 0x00 },
        .msg_len = 4
    };

    if(ioctl(mod->fe_fd, FE_DISEQC_SEND_MASTER_CMD, &cmd) != 0)
    {
        asc_log_error(MSG("diseqc: FE_DISEQC_SEND_MASTER_CMD failed [%s]"), strerror(errno));
        astra_abort();
    }

    nanosleep(&ns, NULL);

    fe_sec_mini_cmd_t burst = ((mod->diseqc - 1) & 1) ? SEC_MINI_B : SEC_MINI_A;
    if(ioctl(mod->fe_fd, FE_DISEQC_SEND_BURST, burst) != 0)
    {
        asc_log_error(MSG("diseqc: FE_DISEQC_SEND_BURST failed [%s]"), strerror(errno));
        astra_abort();
    }

    nanosleep(&ns, NULL);

    if(ioctl(mod->fe_fd, FE_SET_TONE, tone) != 0)
    {
        asc_log_error(MSG("diseqc: FE_SET_TONE failed [%s]"), strerror(errno));
        astra_abort();
    }
} /* diseqc_setup */

/*
 * ooooooooo  ooooo  oooo oooooooooo           oooooooo8
 *  888    88o 888    88   888    888         888
 *  888    888  888  88    888oooo88 ooooooooo 888oooooo
 *  888    888   88888     888    888                 888
 * o888ooo88      888     o888ooo888          o88oooo888
 *
 */

static void fe_tune_s(module_data_t *mod)
{
    int freq = mod->frequency;

    int hiband = 0;
    if(mod->lnb_slof && mod->lnb_lof2 && freq >= mod->lnb_slof)
        hiband = 1;

    if(hiband)
        freq = freq - mod->lnb_lof2;
    else
    {
        if(freq < mod->lnb_lof1)
            freq = mod->lnb_lof1 - freq;
        else
            freq = freq - mod->lnb_lof1;
    }

    int voltage = SEC_VOLTAGE_OFF;
    int tone = SEC_TONE_OFF;
    if(!mod->lnb_sharing)
    {
        voltage = mod->polarization;
        if(hiband || mod->force_tone)
            tone = SEC_TONE_ON;

        if(mod->diseqc)
            diseqc_setup(mod, voltage, tone);
    }

    fe_clear(mod);

    if(mod->type == DVB_TYPE_S)
    {
        struct dvb_frontend_parameters feparams;
        memset(&feparams, 0, sizeof(feparams));
        feparams.frequency = freq;
        feparams.inversion = INVERSION_AUTO;
        feparams.u.qpsk.symbol_rate = mod->symbolrate;
        feparams.u.qpsk.fec_inner = mod->fec;

        if(!mod->diseqc)
        {
            if(ioctl(mod->fe_fd, FE_SET_TONE, tone) != 0)
            {
                asc_log_error(MSG("FE_SET_TONE failed [%s]"), strerror(errno));
                astra_abort();
            }

            if(ioctl(mod->fe_fd, FE_SET_VOLTAGE, voltage) != 0)
            {
                asc_log_error(MSG("FE_SET_VOLTAGE failed [%s]"), strerror(errno));
                astra_abort();
            }
        }

        if(ioctl(mod->fe_fd, FE_SET_FRONTEND, &feparams) != 0)
        {
            asc_log_error(MSG("FE_SET_FRONTEND failed [%s]"), strerror(errno));
            astra_abort();
        }
    }
#if DVB_API_VERSION >= 5
    else
    {
        struct dtv_properties cmdseq;
        struct dtv_property cmdlist[12];

        DTV_PROPERTY_BEGIN(cmdseq, cmdlist);
        DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_DELIVERY_SYSTEM,   SYS_DVBS2);
        DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_FREQUENCY,         freq);
        DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_SYMBOL_RATE,       mod->symbolrate);
        DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_INNER_FEC,         mod->fec);
        DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_INVERSION,         INVERSION_AUTO);
        if(mod->modulation != FE_MODULATION_NONE)
        {
            DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_MODULATION,    mod->modulation);
            DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_ROLLOFF,       mod->rolloff);
        }
        if(!mod->diseqc)
        {
            DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_VOLTAGE,       voltage);
            DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_TONE,          tone);
        }
        DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_PILOT,             PILOT_AUTO);
        DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_TUNE,              0);

        if(ioctl(mod->fe_fd, FE_SET_PROPERTY, &cmdseq) != 0)
        {
            asc_log_error(MSG("FE_SET_PROPERTY DTV_TUNE failed [%s]"), strerror(errno));
            astra_abort();
        }
    }
#endif
}

/*
 * ooooooooo  ooooo  oooo oooooooooo       ooooooooooo
 *  888    88o 888    88   888    888      88  888  88
 *  888    888  888  88    888oooo88 ooooooooo 888
 *  888    888   88888     888    888          888
 * o888ooo88      888     o888ooo888          o888o
 *
 */

static void fe_tune_t(module_data_t *mod)
{
    fe_clear(mod);

    if(mod->type == DVB_TYPE_T)
    {
        struct dvb_frontend_parameters feparams;

        memset(&feparams, 0, sizeof(feparams));
        feparams.frequency = mod->frequency;
        feparams.inversion = INVERSION_AUTO;

        feparams.u.ofdm.code_rate_HP = FEC_AUTO;
        feparams.u.ofdm.code_rate_LP = FEC_AUTO;

        feparams.u.ofdm.bandwidth = mod->bandwidth;
        feparams.u.ofdm.constellation = mod->modulation;
        feparams.u.ofdm.transmission_mode = mod->transmitmode;
        feparams.u.ofdm.guard_interval = mod->guardinterval;
        feparams.u.ofdm.hierarchy_information = mod->hierarchy;

        if(ioctl(mod->fe_fd, FE_SET_FRONTEND, &feparams) != 0)
        {
            asc_log_error(MSG("FE_SET_FRONTEND failed [%s]"), strerror(errno));
            astra_abort();
        }
    }
#if DVB_API_VERSION >= 5
    else
    {
        struct dtv_properties cmdseq;
        struct dtv_property cmdlist[12];

        DTV_PROPERTY_BEGIN(cmdseq, cmdlist);
        DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_DELIVERY_SYSTEM,   SYS_DVBT);
        DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_FREQUENCY,         mod->frequency);
        DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_MODULATION,        mod->modulation);
        DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_INVERSION,         INVERSION_AUTO);

        switch(mod->bandwidth)
        {
            case BANDWIDTH_8_MHZ:
                DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_BANDWIDTH_HZ, 8000000);
                break;
            case BANDWIDTH_7_MHZ:
                DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_BANDWIDTH_HZ, 7000000);
                break;
            case BANDWIDTH_6_MHZ:
                DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_BANDWIDTH_HZ, 6000000);
                break;
            default:
                DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_BANDWIDTH_HZ, 0);
                break;
        }

        DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_CODE_RATE_HP,      FEC_AUTO);
        DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_CODE_RATE_LP,      FEC_AUTO);
        DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_GUARD_INTERVAL,    mod->guardinterval);
        DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_TRANSMISSION_MODE, mod->transmitmode);
        DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_HIERARCHY,         mod->hierarchy);
        DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_TUNE,              0);

        if(ioctl(mod->fe_fd, FE_SET_PROPERTY, &cmdseq) != 0)
        {
            asc_log_error(MSG("FE_SET_PROPERTY DTV_TUNE failed [%s]"), strerror(errno));
            astra_abort();
        }
    }
#endif
}

/*
 * ooooooooo  ooooo  oooo oooooooooo             oooooooo8
 *  888    88o 888    88   888    888          o888     88
 *  888    888  888  88    888oooo88 ooooooooo 888
 *  888    888   88888     888    888          888o     oo
 * o888ooo88      888     o888ooo888            888oooo88
 *
 */

static void fe_tune_c(module_data_t *mod)
{
    struct dvb_frontend_parameters feparams;

    memset(&feparams, 0, sizeof(feparams));
    feparams.frequency = mod->frequency;
    feparams.inversion = INVERSION_AUTO;
    feparams.u.qam.symbol_rate = mod->symbolrate;
    feparams.u.qam.modulation = mod->modulation;
    feparams.u.qam.fec_inner = mod->fec;

    if(ioctl(mod->fe_fd, FE_SET_FRONTEND, &feparams) != 0)
    {
        asc_log_error(MSG("FE_SET_FRONTEND failed [%s]"), strerror(errno));
        astra_abort();
    }
}

/*
 * oooooooooo      o       oooooooo8 ooooooooooo
 *  888    888    888     888         888    88
 *  888oooo88    8  88     888oooooo  888ooo8
 *  888    888  8oooo88           888 888    oo
 * o888ooo888 o88o  o888o o88oooo888 o888ooo8888
 *
 */

static void fe_tune(module_data_t *mod)
{
    switch(mod->type)
    {
        case DVB_TYPE_S:
        case DVB_TYPE_S2:
            fe_tune_s(mod);
            break;
        case DVB_TYPE_T:
        case DVB_TYPE_T2:
            fe_tune_t(mod);
            break;
        case DVB_TYPE_C:
            fe_tune_c(mod);
            break;
        default:
            astra_abort();
    }
}

void fe_open(module_data_t *mod)
{
    char dev_name[32];
    sprintf(dev_name, "/dev/dvb/adapter%d/frontend%d", mod->adapter, mod->device);

    mod->fe_fd = open(dev_name, O_RDWR | O_NONBLOCK);
    if(mod->fe_fd <= 0)
    {
        asc_log_error(MSG("failed to open frontend [%s]"), strerror(errno));
        astra_abort();
    }

    fe_tune(mod);
}

void fe_close(module_data_t *mod)
{
    if(mod->fe_fd > 0)
        close(mod->fe_fd);
}

void fe_loop(module_data_t *mod, int is_data)
{
    if(is_data)
    {
        fe_event(mod);
        return;
    }

    if(!mod->do_retune)
        fe_status(mod);

    if(mod->do_retune)
    {
        mod->do_retune = 0;
        fe_tune(mod);
        sleep(2);
    }
}
