/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#ifndef _MPEGTS_H_
#define _MPEGTS_H_ 1

#include <astra.h>

#define TS_PACKET_SIZE 188
#define TS_HEADER_SIZE 4
#define TS_BODY_SIZE (TS_PACKET_SIZE - TS_HEADER_SIZE)

#define MAX_PID 8192
#define NULL_TS_PID (MAX_PID - 1)
#define PSI_MAX_SIZE 0x00000FFF
#define PES_MAX_SIZE 0x0000FFFF
#define DESC_MAX_SIZE 1024

#define TS_PID(_ts) (((_ts[1] & 0x1f) << 8) | _ts[2])
#define TS_PUSI(_ts) (_ts[1] & 0x40)
#define TS_SC(_ts) (_ts[3] & 0xC0)
#define TS_AF(_ts) (_ts[3] & 0x30)
#define TS_CC(_ts) (_ts[3] & 0x0f)
#define TS_PTR(_ts) (_ts[4])
#define PSI_SIZE(_psi) ((((_psi[1] & 0x0f) << 8) | _psi[2]) + 3)
#define PES_SIZE(_pes) (((_pes[4] << 8) | _pes[5]) + 6)

#define PES_HEADER(_pes) ((_pes[0] << 16) | (_pes[1] << 8) | (_pes[2]))

typedef enum
{
    MPEGTS_PACKET_UNKNOWN   = 0x00000000,
    MPEGTS_PACKET_TS        = 0x01000000,
    MPEGTS_PACKET_PSI       = 0x00100000, // Program Specific Information
    MPEGTS_PACKET_PAT       = MPEGTS_PACKET_PSI | 0x01,
    MPEGTS_PACKET_CAT       = MPEGTS_PACKET_PSI | 0x02,
    MPEGTS_PACKET_PMT       = MPEGTS_PACKET_PSI | 0x04,
    MPEGTS_PACKET_SI        = 0x00200000, // Service Information
    MPEGTS_PACKET_NIT       = MPEGTS_PACKET_SI | 0x01,
    MPEGTS_PACKET_SDT       = MPEGTS_PACKET_SI | 0x02,
    MPEGTS_PACKET_EIT       = MPEGTS_PACKET_SI | 0x04,
    MPEGTS_PACKET_TDT       = MPEGTS_PACKET_SI | 0x08,
    MPEGTS_PACKET_CA        = 0x00400000, // Conditional Access
    MPEGTS_PACKET_ECM       = MPEGTS_PACKET_CA | 0x01,
    MPEGTS_PACKET_EMM       = MPEGTS_PACKET_CA | 0x02,
    MPEGTS_PACKET_PES       = 0x00800000, // Elementary Stream
    MPEGTS_PACKET_VIDEO     = MPEGTS_PACKET_PES | 0x01,
    MPEGTS_PACKET_AUDIO     = MPEGTS_PACKET_PES | 0x02,
    MPEGTS_PACKET_SUB       = MPEGTS_PACKET_PES | 0x04,
    MPEGTS_PACKET_DATA      = 0x01000000
} mpegts_packet_type_t;

typedef enum
{
    MPEGTS_CRC32_CHANGED = -2, // need to clean and rescan
    MPEGTS_UNCHANGED = -1,
    MPEGTS_ERROR_NONE = 0, // ready to scan
    MPEGTS_ERROR_NOT_READY = 1,
    MPEGTS_ERROR_PACKET_TYPE = 2,
    MPEGTS_ERROR_TABLE_ID = 3,
    MPEGTS_ERROR_FIXED_BITS = 4,
    MPEGTS_ERROR_LENGTH = 5,
    MPEGTS_ERROR_CRC32 = 6,
} mpegts_parse_status_t;

/* src/psi.c */

typedef struct
{
    mpegts_packet_type_t type;
    mpegts_parse_status_t status;

    uint16_t pid;
    uint8_t cc;
    uint32_t crc32;

    void *data; // PAT/CAT/PMT

    uint8_t ts[TS_PACKET_SIZE]; // to demux

    size_t buffer_size;
    size_t buffer_skip;
    uint8_t buffer[PSI_MAX_SIZE];
} mpegts_psi_t;

mpegts_psi_t * mpegts_psi_init(mpegts_packet_type_t, uint16_t);
void mpegts_psi_destroy(mpegts_psi_t *);

void mpegts_psi_mux(mpegts_psi_t *, uint8_t *
                    , void (*)(module_data_t *, mpegts_psi_t *)
                    , module_data_t *);

void mpegts_psi_demux(mpegts_psi_t *
                      , void (*)(module_data_t *, uint8_t *)
                      , module_data_t *);

uint32_t mpegts_psi_get_crc(mpegts_psi_t *);
uint32_t mpegts_psi_calc_crc(mpegts_psi_t *);

/* src/desc.c */
typedef struct
{
    list_t *items;

    size_t buffer_size;
    uint8_t buffer[DESC_MAX_SIZE];
} mpegts_desc_t;

mpegts_desc_t * mpegts_desc_init(uint8_t *, size_t);
void mpegts_desc_destroy(mpegts_desc_t *);
void mpegts_desc_parse(mpegts_desc_t *);
void mpegts_desc_dump(mpegts_desc_t *, uint8_t, const char *);
void mpegts_desc_assemble(mpegts_desc_t *);

/* src/pat.c */

typedef struct
{
    uint16_t pnr;
    uint16_t pid;
} mpegts_pat_item_t;

typedef struct
{
    uint16_t stream_id;
    uint8_t version;
    uint8_t current_next;

    list_t *items;
} mpegts_pat_t;

mpegts_psi_t * mpegts_pat_init(void);
void mpegts_pat_destroy(mpegts_psi_t *);

void mpegts_pat_parse(mpegts_psi_t *);
void mpegts_pat_dump(mpegts_psi_t *, const char *);
void mpegts_pat_assemble(mpegts_psi_t *);

void mpegts_pat_item_add(mpegts_psi_t *, uint16_t, uint16_t);
void mpegts_pat_item_delete(mpegts_psi_t *, uint16_t);

/* src/cat.c */

typedef struct
{
    uint8_t version;
    uint8_t current_next;

    mpegts_desc_t *desc;
} mpegts_cat_t;

mpegts_psi_t * mpegts_cat_init(void);
void mpegts_cat_destroy(mpegts_psi_t *);

void mpegts_cat_parse(mpegts_psi_t *);
void mpegts_cat_dump(mpegts_psi_t *, const char *);

/* src/pmt.c */

typedef struct
{
    uint8_t type;
    uint16_t pid;

    mpegts_desc_t *desc;
} mpegts_pmt_item_t;

typedef struct
{
    uint16_t pnr;
    uint16_t pcr;
    uint8_t version;
    uint8_t current_next;

    mpegts_desc_t *desc;

    list_t *items;
} mpegts_pmt_t;

mpegts_psi_t * mpegts_pmt_init(uint16_t);
mpegts_psi_t * mpegts_pmt_duplicate(mpegts_psi_t *);
void mpegts_pmt_destroy(mpegts_psi_t *);

void mpegts_pmt_parse(mpegts_psi_t *);
void mpegts_pmt_dump(mpegts_psi_t *, const char *);
void mpegts_pmt_assemble(mpegts_psi_t *);

void mpegts_pmt_item_add(mpegts_psi_t *, uint16_t, uint8_t, mpegts_desc_t *);
void mpegts_pmt_item_delete(mpegts_psi_t *, uint16_t);
mpegts_pmt_item_t * mpegts_pmt_item_get(mpegts_psi_t *, uint16_t);

/* src/sdt.c */

typedef struct
{
    uint16_t pnr;

    mpegts_desc_t *desc;
} mpegts_sdt_item_t;

typedef struct
{
    uint16_t stream_id;
    uint8_t version;
    uint8_t current_next;
    uint16_t network_id;

    list_t *items;
} mpegts_sdt_t;

mpegts_psi_t * mpegts_sdt_init(void);
void mpegts_sdt_destroy(mpegts_psi_t *);

void mpegts_sdt_parse(mpegts_psi_t *);
void mpegts_sdt_dump(mpegts_psi_t *, const char *);

/* src/stream.c */

typedef mpegts_psi_t *mpegts_stream_t[MAX_PID];

void mpegts_stream_destroy(mpegts_stream_t);

/* src/pes.c */

typedef struct
{
    mpegts_packet_type_t type;

    uint16_t pid;
    uint8_t cc;

    uint8_t stream_id;
    uint8_t pts;

    uint8_t ts[TS_PACKET_SIZE]; // to demux

    size_t buffer_size;
    size_t buffer_skip;
    uint8_t buffer[PES_MAX_SIZE];
} mpegts_pes_t;

mpegts_pes_t * mpegts_pes_init(mpegts_packet_type_t, uint16_t);
void mpegts_pes_destroy(mpegts_pes_t *);

void mpegts_pes_mux(mpegts_pes_t *, uint8_t *
                    , void (*)(module_data_t *, mpegts_pes_t *)
                    , module_data_t *);

void mpegts_pes_demux(mpegts_pes_t *
                      , void (*)(module_data_t *, uint8_t *)
                      , module_data_t *);


mpegts_packet_type_t mpegts_pes_type(uint8_t);
const char * mpegts_pes_name(mpegts_packet_type_t);

void mpegts_pes_add_data(mpegts_pes_t *, const uint8_t *, size_t);

/* src/crc32b.c */
#define CRC32_SIZE 4
uint32_t crc32b(const uint8_t *, size_t);

#endif /* _MPEGTS_H_ */
