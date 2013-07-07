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

#define ioctl(_a, _b, _c) 1
#include "linux/dvb/version.h"
#include "linux/dvb/frontend.h"
#include "linux/dvb/dmx.h"
#include "linux/dvb/ca.h"

#if DVB_API_VERSION < 5
#   error "DVB_API_VERSION < 5"
#endif

#define DVB_API ((DVB_API_VERSION * 100) + DVB_API_VERSION_MINOR)
#define DVR_BUFFER_SIZE (1022 * TS_PACKET_SIZE)

#define FE_MODULATION_NONE 0xFFFF

typedef enum
{
    DVB_TYPE_UNKNOWN = 0,
    DVB_TYPE_S, DVB_TYPE_S2,
    DVB_TYPE_T, DVB_TYPE_T2,
    DVB_TYPE_C
} dvb_type_t;

typedef struct
{
    int is_active;
} ca_slot_t;

struct module_data_t
{
    MODULE_LUA_DATA();
    MODULE_STREAM_DATA();

    /* Base Config */
    dvb_type_t type;
    int adapter;
    int device;

    /* Base */
    asc_thread_t *thread;

    /* DVR Config */
    int dvr_buffer_size;

    /* DVR Base */
    int dvr_fd;
    asc_event_t *dvr_event;
    uint8_t dvr_buffer[DVR_BUFFER_SIZE];

    uint32_t dvr_read;

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

    /* DMX config */
    int dmx_budget;

    /* DMX Base */
    char dmx_dev_name[32];
    int *dmx_fd_list;

    /* CA Base */
    int ca_fd;
    ca_slot_t *ca;
};

#define MSG(_msg) "[dvb_input %d:%d] " _msg, mod->adapter, mod->device

void dvb_thread_open(module_data_t *mod);
void dvb_thread_close(module_data_t *mod);

void fe_open(module_data_t *mod);
void fe_close(module_data_t *mod);
void fe_loop(module_data_t *mod, int is_data);

void ca_open(module_data_t *mod);
void ca_close(module_data_t *mod);
void ca_loop(module_data_t *mod, int is_data);

void dvr_open(module_data_t *mod);
void dvr_close(module_data_t *mod);

void dmx_open(module_data_t *mod);
void dmx_close(module_data_t *mod);
void dmx_bounce(module_data_t *mod);
void dmx_set_pid(module_data_t *mod, uint16_t pid, int is_set);
