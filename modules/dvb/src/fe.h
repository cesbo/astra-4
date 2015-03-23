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

#ifndef _FE_H_
#define _FE_H_ 1

#include "../dvb.h"

/*
 * Polarization:
 * H/L - SEC_VOLTAGE_18
 * V/R - SEC_VOLTAGE_13
 */

typedef struct dvb_fe_t dvb_fe_t;

#define FE_MODULATION_NONE 0xFFFF

typedef enum
{
    DVB_TYPE_UNKNOWN = 0,
    DVB_TYPE_S,
    DVB_TYPE_T,
    DVB_TYPE_C,
    DVB_TYPE_ATSC,
} dvb_type_t;

struct dvb_fe_t
{
    /* General Config */
    dvb_type_t type;
    int adapter;
    int device;
    int timeout;

    /* FE Config */
    fe_delivery_system_t delivery_system;
    fe_modulation_t modulation;

    int frequency;
    int symbolrate;

    int tone;
    int voltage;

    int diseqc;

    int uni_frequency;
    int uni_scr;

    fe_code_rate_t fec;
    fe_rolloff_t rolloff;
    int stream_id;

    fe_bandwidth_t bandwidth;
    fe_guard_interval_t guardinterval;
    fe_transmit_mode_t transmitmode;
    fe_hierarchy_t hierarchy;

    bool raw_signal;
    bool log_signal;

    /* FE Base */
    int fe_fd;

    int do_retune;

    /* FE Status */
    bool is_started;
    fe_status_t status;
    bool lock;
    int signal;
    int snr;
    int ber;
    int unc;
};

void fe_open(dvb_fe_t *fe);
void fe_close(dvb_fe_t *fe);
void fe_loop(dvb_fe_t *fe, int is_data);

#endif /* _FE_H_ */
