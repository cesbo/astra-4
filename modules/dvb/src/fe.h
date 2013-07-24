/*
 * Astra DVB Module
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
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

typedef enum
{
    DVB_TYPE_UNKNOWN = 0,
    DVB_TYPE_S, DVB_TYPE_S2,
    DVB_TYPE_T, DVB_TYPE_T2,
    DVB_TYPE_C
} dvb_type_t;

#define FE_MODULATION_NONE 0xFFFF

struct dvb_fe_t
{
    /* General Config */
    dvb_type_t type;
    int adapter;
    int device;

    /* FE Config */
    int frequency;
    fe_modulation_t modulation;

    fe_sec_voltage_t polarization;
    int symbolrate;
    int lnb_lof1;
    int lnb_lof2;
    int lnb_slof;
    int lnb_sharing;

    int diseqc;
    int force_tone;

    fe_code_rate_t fec;
#if DVB_API_VERSION >= 5
    fe_rolloff_t rolloff;
#endif

    fe_bandwidth_t bandwidth;
    fe_guard_interval_t guardinterval;
    fe_transmit_mode_t transmitmode;
    fe_hierarchy_t hierarchy;

    /* FE Base */
    int fe_fd;

    int do_retune;

    /* FE Status */
    fe_status_t fe_status;
    int lock;
    int signal;
    int snr;
    int ber;
    int unc;
};

void fe_open(dvb_fe_t *fe);
void fe_close(dvb_fe_t *fe);
void fe_loop(dvb_fe_t *fe, int is_data);

#endif /* _FE_H_ */
