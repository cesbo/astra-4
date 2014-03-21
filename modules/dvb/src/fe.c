/*
 * Astra Module: DVB (Frontend)
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

#include "fe.h"

#include <fcntl.h>

#define MSG(_msg) "[dvb_input %d:%d] " _msg, fe->adapter, fe->device

#define DTV_PROPERTY_BEGIN(_cmdseq, _cmdlist)                                                   \
    _cmdseq.num = 0;                                                                            \
    _cmdseq.props = _cmdlist

#define DTV_PROPERTY_SET(_cmdseq, _cmdlist, _cmd, _data)                                        \
    _cmdlist[_cmdseq.num].cmd = _cmd;                                                           \
    _cmdlist[_cmdseq.num].u.data = _data;                                                       \
    ++_cmdseq.num

static void fe_clear(dvb_fe_t *fe)
{
    static struct dtv_property clear[] = { { .cmd = DTV_CLEAR } };
    static struct dtv_properties cmdclear = { .num = 1, .props = clear };
    if(ioctl(fe->fe_fd, FE_SET_PROPERTY, &cmdclear ) != 0)
    {
        asc_log_error(MSG("FE_SET_PROPERTY DTV_CLEAR failed [%s]"), strerror(errno));
        astra_abort();
    }

    struct dvb_frontend_event fe_event;
    while(ioctl(fe->fe_fd, FE_GET_EVENT, &fe_event) != -1)
        ;
}

/*
 *  oooooooo8 ooooooooooo   o   ooooooooooo ooooo  oooo oooooooo8
 * 888        88  888  88  888  88  888  88  888    88 888
 *  888oooooo     888     8  88     888      888    88  888oooooo
 *         888    888    8oooo88    888      888    88         888
 * o88oooo888    o888o o88o  o888o o888o      888oo88  o88oooo888
 *
 */

static void fe_read_status(dvb_fe_t *fe)
{
    if(ioctl(fe->fe_fd, FE_READ_SIGNAL_STRENGTH, &fe->signal) != 0)
        fe->signal = -2;
    else
        fe->signal = (fe->signal * 100) / 0xFFFF;

    if(ioctl(fe->fe_fd, FE_READ_SNR, &fe->snr) != 0)
        fe->snr = -2;
    else
        fe->snr = (fe->snr * 100) / 0xFFFF;

    if(ioctl(fe->fe_fd, FE_READ_BER, &fe->ber) != 0)
        fe->ber = -2;

    if(ioctl(fe->fe_fd, FE_READ_UNCORRECTED_BLOCKS, &fe->unc) != 0)
        fe->unc = -2;
}

static void fe_check_status(dvb_fe_t *fe)
{
    fe_status_t fe_status;
    if(ioctl(fe->fe_fd, FE_READ_STATUS, &fe_status) != 0)
    {
        asc_log_error(MSG("FE_READ_STATUS failed [%s]"), strerror(errno));
        astra_abort();
    }

    fe->lock = fe_status & FE_HAS_LOCK;
    if(!fe->lock)
    {
        fe->do_retune = 1;
        return;
    }

    fe_read_status(fe);
}

/*
 * ooooooooooo ooooo  oooo ooooooooooo oooo   oooo ooooooooooo
 *  888    88   888    88   888    88   8888o  88  88  888  88
 *  888ooo8      888  88    888ooo8     88 888o88      888
 *  888    oo     88888     888    oo   88   8888      888
 * o888ooo8888     888     o888ooo8888 o88o    88     o888o
 *
 */

static void fe_event(dvb_fe_t *fe)
{
    struct dvb_frontend_event dvb_fe_event;
    fe_status_t fe_status, fe_status_diff;

    while(1) /* read all events */
    {
        if(ioctl(fe->fe_fd, FE_GET_EVENT, &dvb_fe_event) != 0)
        {
            if(errno == EWOULDBLOCK)
                return;
            asc_log_error(MSG("FE_GET_EVENT failed [%s]"), strerror(errno));
            return;
        }

        fe_status = dvb_fe_event.status;
        fe_status_diff = fe_status ^ fe->fe_status;
        fe->fe_status = fe_status;

        if(fe_status_diff & FE_REINIT)
        {
            if(fe_status & FE_REINIT)
            {
                asc_log_warning(MSG("fe was reinitialized"));
                fe_clear(fe);
                fe->do_retune = 1;
                return;
            }
        }

        const char ss = (fe_status & FE_HAS_SIGNAL) ? 'S' : '_';
        const char sc = (fe_status & FE_HAS_CARRIER) ? 'C' : '_';
        const char sv = (fe_status & FE_HAS_VITERBI) ? 'V' : '_';
        const char sy = (fe_status & FE_HAS_SYNC) ? 'Y' : '_';
        const char sl = (fe_status & FE_HAS_LOCK) ? 'L' : '_';

        if(fe_status_diff & FE_HAS_LOCK)
        {
            fe->lock = fe_status & FE_HAS_LOCK;
            if(fe->lock)
            {
                fe_read_status(fe);

                asc_log_info(MSG("fe has lock. status:%c%c%c%c%c signal:%d%% snr:%d%%")
                             , ss, sc, sv, sy, sl
                             , fe->signal, fe->snr);

                fe->do_retune = 0;
            }
            else
            {
                asc_log_warning(MSG("fe has lost lock. status:%c%c%c%c%c")
                                , ss, sc, sv, sy, sl);
                fe_clear(fe);
                fe->do_retune = 1;
                return;
            }
        }
    }
}

/*
 * ooooooooo   o88    oooooooo8 ooooooooooo              oooooooo8
 *  888    88o oooo  888         888    88   ooooooooo o888     88
 *  888    888  888   888oooooo  888ooo8   888    888  888
 *  888    888  888          888 888    oo 888    888  888o     oo
 * o888ooo88   o888o o88oooo888 o888ooo8888  88ooo888   888oooo88
 *                                                888o
 */

static void diseqc_setup(dvb_fe_t *fe, int voltage, int tone)
{
    static struct timespec ns = { .tv_sec = 0, .tv_nsec = 15 * 1000 * 1000 };

    if(ioctl(fe->fe_fd, FE_SET_TONE, SEC_TONE_OFF) != 0)
    {
        asc_log_error(MSG("diseqc: FE_SET_TONE failed [%s]"), strerror(errno));
        astra_abort();
    }

    if(ioctl(fe->fe_fd, FE_SET_VOLTAGE, voltage) != 0)
    {
        asc_log_error(MSG("diseqc: FE_SET_VOLTAGE failed [%s]"), strerror(errno));
        astra_abort();
    }

    nanosleep(&ns, NULL);

    const int data0 = 0xF0
                    | ((fe->diseqc - 1) << 2)
                    | ((voltage == SEC_VOLTAGE_18) << 1)
                    | (tone == SEC_TONE_ON);

    struct dvb_diseqc_master_cmd cmd =
    {
        .msg = { 0xE0, 0x10, 0x38, data0, 0x00, 0x00 },
        .msg_len = 4
    };

    if(ioctl(fe->fe_fd, FE_DISEQC_SEND_MASTER_CMD, &cmd) != 0)
    {
        asc_log_error(MSG("diseqc: FE_DISEQC_SEND_MASTER_CMD failed [%s]"), strerror(errno));
        astra_abort();
    }

    nanosleep(&ns, NULL);

    fe_sec_mini_cmd_t burst = ((fe->diseqc - 1) & 1) ? SEC_MINI_B : SEC_MINI_A;
    if(ioctl(fe->fe_fd, FE_DISEQC_SEND_BURST, burst) != 0)
    {
        asc_log_error(MSG("diseqc: FE_DISEQC_SEND_BURST failed [%s]"), strerror(errno));
        astra_abort();
    }

    nanosleep(&ns, NULL);

    if(ioctl(fe->fe_fd, FE_SET_TONE, tone) != 0)
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

static void fe_tune_s(dvb_fe_t *fe)
{
    int freq = fe->frequency;

    int hiband = 0;
    if(fe->lnb_slof && fe->lnb_lof2 && freq >= fe->lnb_slof)
        hiband = 1;

    if(hiband)
        freq = freq - fe->lnb_lof2;
    else
    {
        if(freq < fe->lnb_lof1)
            freq = fe->lnb_lof1 - freq;
        else
            freq = freq - fe->lnb_lof1;
    }

    int voltage = SEC_VOLTAGE_OFF;
    int tone = SEC_TONE_OFF;
    if(!fe->lnb_sharing)
    {
        voltage = fe->polarization;
        if(hiband || fe->force_tone)
            tone = SEC_TONE_ON;

        if(fe->diseqc)
            diseqc_setup(fe, voltage, tone);
    }

    struct dtv_properties cmdseq;
    struct dtv_property cmdlist[13];

    const fe_delivery_system_t dvb_sys = (fe->type == DVB_TYPE_S) ? SYS_DVBS : SYS_DVBS2;

    DTV_PROPERTY_BEGIN(cmdseq, cmdlist);
    DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_DELIVERY_SYSTEM,   dvb_sys);
    DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_FREQUENCY,         freq);
    DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_SYMBOL_RATE,       fe->symbolrate);
    DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_INNER_FEC,         fe->fec);
    DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_INVERSION,         INVERSION_AUTO);
    if(fe->modulation != FE_MODULATION_NONE)
    {
        DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_MODULATION,    fe->modulation);
    }
    DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_ROLLOFF,           fe->rolloff);
    DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_STREAM_ID,         fe->stream_id);
    if(!fe->diseqc)
    {
        DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_VOLTAGE,       voltage);
        DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_TONE,          tone);
    }
    DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_PILOT,             PILOT_AUTO);
    DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_TUNE,              0);

    if(ioctl(fe->fe_fd, FE_SET_PROPERTY, &cmdseq) != 0)
    {
        asc_log_error(MSG("FE_SET_PROPERTY DTV_TUNE failed [%s]"), strerror(errno));
        astra_abort();
    }
}

/*
 * ooooooooo  ooooo  oooo oooooooooo       ooooooooooo
 *  888    88o 888    88   888    888      88  888  88
 *  888    888  888  88    888oooo88 ooooooooo 888
 *  888    888   88888     888    888          888
 * o888ooo88      888     o888ooo888          o888o
 *
 */

static void fe_tune_t(dvb_fe_t *fe)
{
    struct dtv_properties cmdseq;
    struct dtv_property cmdlist[13];

    const fe_delivery_system_t dvb_sys = (fe->type == DVB_TYPE_T2) ? SYS_DVBT2 : SYS_DVBT;

    DTV_PROPERTY_BEGIN(cmdseq, cmdlist);
    DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_DELIVERY_SYSTEM,   dvb_sys);
    DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_FREQUENCY,         fe->frequency);
    DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_MODULATION,        fe->modulation);
    DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_INVERSION,         INVERSION_AUTO);

    switch(fe->bandwidth)
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
    DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_GUARD_INTERVAL,    fe->guardinterval);
    DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_TRANSMISSION_MODE, fe->transmitmode);
    DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_HIERARCHY,         fe->hierarchy);
    DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_STREAM_ID,         fe->stream_id);
    DTV_PROPERTY_SET(cmdseq, cmdlist, DTV_TUNE,              0);

    if(ioctl(fe->fe_fd, FE_SET_PROPERTY, &cmdseq) != 0)
    {
        asc_log_error(MSG("FE_SET_PROPERTY DTV_TUNE failed [%s]"), strerror(errno));
        astra_abort();
    }
}

/*
 * ooooooooo  ooooo  oooo oooooooooo             oooooooo8
 *  888    88o 888    88   888    888          o888     88
 *  888    888  888  88    888oooo88 ooooooooo 888
 *  888    888   88888     888    888          888o     oo
 * o888ooo88      888     o888ooo888            888oooo88
 *
 */

static void fe_tune_c(dvb_fe_t *fe)
{
    struct dvb_frontend_parameters feparams;

    memset(&feparams, 0, sizeof(feparams));
    feparams.frequency = fe->frequency;
    feparams.inversion = INVERSION_AUTO;
    feparams.u.qam.symbol_rate = fe->symbolrate;
    feparams.u.qam.modulation = fe->modulation;
    feparams.u.qam.fec_inner = fe->fec;

    if(ioctl(fe->fe_fd, FE_SET_FRONTEND, &feparams) != 0)
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

static void fe_tune(dvb_fe_t *fe)
{
    fe_clear(fe);
    fe->do_retune = 6;

    switch(fe->type)
    {
        case DVB_TYPE_S:
        case DVB_TYPE_S2:
            fe_tune_s(fe);
            break;
        case DVB_TYPE_T:
        case DVB_TYPE_T2:
            fe_tune_t(fe);
            break;
        case DVB_TYPE_C:
            fe_tune_c(fe);
            break;
        default:
            astra_abort();
    }
}

void fe_open(dvb_fe_t *fe)
{
    char dev_name[32];
    sprintf(dev_name, "/dev/dvb/adapter%d/frontend%d", fe->adapter, fe->device);

    fe->fe_fd = open(dev_name, O_RDWR | O_NONBLOCK);
    if(fe->fe_fd <= 0)
    {
        asc_log_error(MSG("failed to open frontend [%s]"), strerror(errno));
        astra_abort();
    }

    fe_tune(fe);
}

void fe_close(dvb_fe_t *fe)
{
    if(fe->fe_fd > 0)
        close(fe->fe_fd);
}

void fe_loop(dvb_fe_t *fe, int is_data)
{
    if(is_data)
    {
        fe_event(fe);
    }
    else
    {
        if(fe->do_retune == 0)
            fe_check_status(fe);
        else if(fe->do_retune == 1)
            fe_tune(fe);
        else
            --fe->do_retune;
    }
}
