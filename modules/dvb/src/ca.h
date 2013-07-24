/*
 * Astra DVB Module
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#ifndef _CA_H_
#define _CA_H_ 1

#include "../dvb.h"

typedef struct dvb_ca_t dvb_ca_t;

// 1 - sessions[0] is empty
#define MAX_SESSIONS (32 + 1)
#define MAX_TPDU_SIZE 2048

typedef struct
{
    uint8_t buffer[MAX_TPDU_SIZE];
    uint32_t buffer_size;
} ca_tpdu_message_t;

typedef struct
{
    uint32_t resource_id;

    void (*event)(dvb_ca_t *ca, uint8_t slot_id, uint16_t session_id);
    void (*close)(dvb_ca_t *ca, uint8_t slot_id, uint16_t session_id);
    void (*manage)(dvb_ca_t *ca, uint8_t slot_id, uint16_t session_id);

    void *data;
} ca_session_t;

typedef struct
{
    bool is_active;
    bool is_busy;
    bool is_first_ca_pmt;

    // send
    asc_list_t *queue;

    // recv
    uint8_t buffer[MAX_TPDU_SIZE];
    uint16_t buffer_size;

    // session
    uint16_t pending_session_id;
    ca_session_t sessions[MAX_SESSIONS];
} ca_slot_t;

typedef struct
{
    uint16_t pnr;
    uint32_t crc;

    uint8_t buffer[PSI_MAX_SIZE];
    uint16_t buffer_size;
} ca_pmt_t;

struct dvb_ca_t
{
    int adapter;
    int device;

    /* CA Base */
    int ca_fd;
    int slots_num;
    ca_slot_t *slots;

    uint8_t ca_buffer[MAX_TPDU_SIZE];

    /* CA PMT */
    bool ca_ready;

    mpegts_packet_type_t stream[MAX_PID];
    mpegts_psi_t *pat;
    mpegts_psi_t *pmt;

    asc_list_t *ca_pmt_list;
};

void ca_on_ts(dvb_ca_t *ca, const uint8_t *ts);
void ca_append_pnr(dvb_ca_t *ca, uint16_t pnr);
void ca_remove_pnr(dvb_ca_t *ca, uint16_t pnr);
void ca_open(dvb_ca_t *ca);
void ca_close(dvb_ca_t *ca);
void ca_loop(dvb_ca_t *ca, int is_data);

#endif /* _CA_H_ */
