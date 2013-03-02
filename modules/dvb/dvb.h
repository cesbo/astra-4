/*
 * Astra DVB Module
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

/*
 * Polarization:
 * H/L - SEC_VOLTAGE_18
 * V/R - SEC_VOLTAGE_13
 */

#include <astra.h>

#include <sys/ioctl.h>
#include <linux/dvb/version.h>
#include <linux/dvb/frontend.h>

#define DVB_API ((DVB_API_VERSION * 100) + DVB_API_VERSION_MINOR)

typedef enum
{
    DVB_TYPE_UNKNOWN = 0,
    DVB_TYPE_S, DVB_TYPE_T, DVB_TYPE_C,
    DVB_TYPE_S2,
    DVB_TYPE_T2
} dvb_type_t;

struct module_data_t
{
    MODULE_STREAM_DATA();
    MODULE_DEMUX_DATA();

    /* config */
    dvb_type_t type;
    int adapter;
    int device;

    int frequency;

    fe_sec_voltage_t polarization;
    int symbolrate;

    int lnb_lof1;
    int lnb_lof2;
    int lnb_slof;
    int lnb_sharing;

    int diseqc;
    int force_tone;

    fe_modulation_t modulation;
    fe_code_rate_t fec;
#if DVB_API_VERSION >= 5
    fe_rolloff_t rolloff;
#endif

    /* FE */
    asc_thread_t *fe_thread;

    int fe_fd;
    fe_status_t fe_status;

    int lock;
    int signal;
    int snr;
    int ber;
    int unc;
};

#define MSG(_msg) "[dvb_input %d:%d] " _msg, mod->adapter, mod->device

void frontend_open(module_data_t *mod);
void frontend_close(module_data_t *mod);
