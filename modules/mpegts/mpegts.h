/*
 * Astra MPEG-TS Modules Set
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#ifndef _MPEGTS_H_
#define _MPEGTS_H_ 1

#include <stdint.h>
#include <modules/astra/base.h> /* crc32b, hex_to_str */

/*
 * ooooooooooo  oooooooo8
 * 88  888  88 888
 *     888      888oooooo
 *     888             888
 *    o888o    o88oooo888
 *
 */

#define TS_PACKET_SIZE 188
#define TS_HEADER_SIZE 4
#define TS_BODY_SIZE (TS_PACKET_SIZE - TS_HEADER_SIZE)

#define M2TS_PACKET_SIZE 192

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
#define TS_PTR(_ts) ((TS_AF(_ts) == 0x10)                                                       \
                     ? &_ts[TS_HEADER_SIZE]                                                     \
                     : ((TS_AF(_ts) == 0x30)                                                    \
                        ? (&_ts[TS_HEADER_SIZE] + _ts[4] + 1)                                   \
                        : NULL                                                                  \
                       )                                                                        \
                    )

/*
 * ooooooooooo ooooo  oooo oooooooooo ooooooooooo  oooooooo8
 * 88  888  88   888  88    888    888 888    88  888
 *     888         888      888oooo88  888ooo8     888oooooo
 *     888         888      888        888    oo          888
 *    o888o       o888o    o888o      o888ooo8888 o88oooo888
 *
 */

typedef enum
{
    MPEGTS_PACKET_UNKNOWN   = 0x00000000,
    MPEGTS_PACKET_TS        = 0x10000000,
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

const char * mpegts_type_name(mpegts_packet_type_t type);
mpegts_packet_type_t mpegts_pes_type(uint8_t type_id);

const char * mpeg4_profile_level_name(uint8_t type_id);

void mpegts_desc_to_string(char *str, uint32_t len, const uint8_t *desc);

/*
 * oooooooooo   oooooooo8 ooooo
 *  888    888 888         888
 *  888oooo88   888oooooo  888
 *  888                888 888
 * o888o       o88oooo888 o888o
 *
 */

#define PSI_SIZE(_psi_buffer) (3 + (((_psi_buffer[1] & 0x0f) << 8) | _psi_buffer[2]))

typedef struct
{
    mpegts_packet_type_t type;
    uint16_t pid;
    uint8_t cc;

    uint32_t crc32;

    // demux
    uint8_t ts[TS_PACKET_SIZE];

    // mux
    uint16_t buffer_size;
    uint16_t buffer_skip;
    uint8_t buffer[PSI_MAX_SIZE];
} mpegts_psi_t;

mpegts_psi_t * mpegts_psi_init(mpegts_packet_type_t type, uint16_t pid);
void mpegts_psi_destroy(mpegts_psi_t *psi);

void mpegts_psi_mux(mpegts_psi_t *psi, const uint8_t *ts
                    , void (*callback)(void *, mpegts_psi_t *)
                    , void *arg);

void mpegts_psi_demux(mpegts_psi_t *psi
                      , void (*callback)(void *, const uint8_t *)
                      , void *arg);

#define PSI_CALC_CRC32(_psi) crc32b(_psi->buffer, _psi->buffer_size - CRC32_SIZE)

// with inline function we have nine more instructions
#define PSI_GET_CRC32(_psi) (                                                                   \
    (_psi->buffer[_psi->buffer_size - CRC32_SIZE + 0] << 24) |                                  \
    (_psi->buffer[_psi->buffer_size - CRC32_SIZE + 1] << 16) |                                  \
    (_psi->buffer[_psi->buffer_size - CRC32_SIZE + 2] << 8 ) |                                  \
    (_psi->buffer[_psi->buffer_size - CRC32_SIZE + 3]      ) )

#define PSI_SET_CRC32(_psi)                                                                     \
    {                                                                                           \
        const uint32_t __crc = PSI_CALC_CRC32(_psi);                                            \
        _psi->buffer[_psi->buffer_size - CRC32_SIZE + 0] = __crc >> 24;                         \
        _psi->buffer[_psi->buffer_size - CRC32_SIZE + 1] = __crc >> 16;                         \
        _psi->buffer[_psi->buffer_size - CRC32_SIZE + 2] = __crc >> 8;                          \
        _psi->buffer[_psi->buffer_size - CRC32_SIZE + 3] = __crc & 0xFF;                        \
    }

#define PSI_SET_SIZE(_psi)                                                                      \
    {                                                                                           \
        const uint16_t __size = _psi->buffer_size - 3;                                          \
        _psi->buffer[1] = (_psi->buffer[1] & 0xF0) | (__size >> 8);                             \
        _psi->buffer[2] = (__size & 0xFF);                                                      \
    }

/*
 * oooooooooo ooooooooooo  oooooooo8
 *  888    888 888    88  888
 *  888oooo88  888ooo8     888oooooo
 *  888        888    oo          888
 * o888o      o888ooo8888 o88oooo888
 *
 */

#define PES_SIZE(_pes) (((_pes[4] << 8) | _pes[5]) + 6)
#define PES_HEADER(_pes) ((_pes[0] << 16) | (_pes[1] << 8) | (_pes[2]))

typedef struct
{
    mpegts_packet_type_t type;
    uint16_t pid;
    uint8_t cc;

    uint8_t stream_id;
    uint8_t pts;

    // demux
    uint8_t ts[TS_PACKET_SIZE];

    // mux
    uint32_t buffer_size;
    uint32_t buffer_skip;
    uint8_t buffer[PES_MAX_SIZE];
} mpegts_pes_t;

mpegts_pes_t * mpegts_pes_init(mpegts_packet_type_t type, uint16_t pid);
void mpegts_pes_destroy(mpegts_pes_t *pes);

void mpegts_pes_mux(mpegts_pes_t *pes, const uint8_t *ts
                    , void (*callback)(void *, mpegts_pes_t *)
                    , void *arg);

void mpegts_pes_demux(mpegts_pes_t *pes
                      , void (*callback)(void *, uint8_t *)
                      , void *arg);

void mpegts_pes_add_data(mpegts_pes_t *pes, const uint8_t *data, uint32_t data_size);

/*
 * ooooooooo  ooooooooooo  oooooooo8    oooooooo8
 *  888    88o 888    88  888         o888     88
 *  888    888 888ooo8     888oooooo  888
 *  888    888 888    oo          888 888o     oo
 * o888ooo88  o888ooo8888 o88oooo888   888oooo88
 *
 */

#define DESC_CA_CAID(_desc) ((_desc[2] << 8) | _desc[3])
#define DESC_CA_PID(_desc) (((_desc[4] & 0x1F) << 8) | _desc[5])

/*
 * oooooooooo   o   ooooooooooo
 *  888    888 888  88  888  88
 *  888oooo88 8  88     888
 *  888      8oooo88    888
 * o888o   o88o  o888o o888o
 *
 */

 #define PAT_INIT(_psi, _tsid, _version)                                                        \
    {                                                                                           \
        _psi->type = MPEGTS_PACKET_PAT;                                                         \
        _psi->pid = 0;                                                                          \
        uint8_t *__buffer = _psi->buffer;                                                       \
        __buffer[0] = 0x00;                         /* table_id */                              \
        __buffer[1] = 0x80 | 0x30;                  /* section_syntax_indicator | reserved */   \
        const uint16_t __stream_id = _tsid;                                                     \
        __buffer[3] = __stream_id >> 8;                                                         \
        __buffer[4] = __stream_id & 0xFF;                                                       \
        __buffer[5] = ((_version << 1) & 0x3E) | 1; /* version | current_next_indicator */      \
        __buffer[6] = __buffer[7] = 0x00;                                                       \
        _psi->buffer_size = 8 + CRC32_SIZE;                                                     \
        PSI_SET_SIZE(_psi);                                                                     \
    }

#define PAT_GET_TSID(_psi) ((_psi->buffer[3] << 8) | _psi->buffer[4])
#define PAT_SET_TSID(_psi, _tsid)                                                               \
    {                                                                                           \
        const uint16_t __tsid = _tsid;                                                          \
        _psi->buffer[3] = __tsid >> 8;                                                          \
        _psi->buffer[4] = __tsid & 0xFF;                                                        \
    }

#define PAT_GET_VERSION(_psi) ((_psi->buffer[5] & 0x3E) >> 1)
#define PAT_SET_VERSION(_psi, _version)                                                         \
    {                                                                                           \
        const uint8_t __version = _version;                                                     \
        _psi->buffer[5] &= ~0x3E;                                                               \
        _psi->buffer[5] |= ((__version << 1) & 0x3E);                                           \
    }

#define PAT_ITEMS_FIRST(_psi) (&_psi->buffer[8])
#define PAT_ITEMS_EOL(_psi, _pointer)                                                           \
    ((_pointer - _psi->buffer) >= (_psi->buffer_size - CRC32_SIZE))
#define PAT_ITEMS_NEXT(_psi, _pointer) _pointer += 4

#define PAT_ITEMS_GET_PNR(_psi, _pointer) ((_pointer[0] << 8) | _pointer[1])
#define PAT_ITEMS_GET_PID(_psi, _pointer) (((_pointer[2] & 0x1F) << 8) | _pointer[3])

#define PAT_ITEMS_APPEND(_psi, _pointer, _pnr, _pid)                                            \
    {                                                                                           \
        const uint16_t __pnr = _pnr;                                                            \
        _pointer[0] = __pnr >> 8;                                                               \
        _pointer[1] = __pnr & 0xFF;                                                             \
        const uint16_t __pid = _pid;                                                            \
        _pointer[2] = (__pid >> 8) & 0x1F;                                                      \
        _pointer[3] = __pid & 0xFF;                                                             \
        _pointer += 4;                                                                          \
        _psi->buffer_size = _pointer - _psi->buffer + CRC32_SIZE;                               \
        PSI_SET_SIZE(_psi);                                                                     \
    }

/*
 *   oooooooo8     o   ooooooooooo
 * o888     88    888  88  888  88
 * 888           8  88     888
 * 888o     oo  8oooo88    888
 *  888oooo88 o88o  o888o o888o
 *
 */

#define CAT_GET_VERSION(_psi) PAT_GET_VERSION(_psi)
#define CAT_SET_VERSION(_psi, _version) PAT_SET_VERSION(_psi, _version)

#define CAT_DESC_FIRST(_psi) (&_psi->buffer[8])
#define CAT_DESC_EOL(_psi, _desc_pointer) PAT_ITEMS_EOL(_psi, _desc_pointer)
#define CAT_DESC_NEXT(_psi, _desc_pointer) _desc_pointer += 2 + _desc_pointer[1]

/*
 * oooooooooo oooo     oooo ooooooooooo
 *  888    888 8888o   888  88  888  88
 *  888oooo88  88 888o8 88      888
 *  888        88  888  88      888
 * o888o      o88o  8  o88o    o888o
 *
 */

#define PMT_GET_PNR(_psi) ((_psi->buffer[3] << 8) | _psi->buffer[4])
#define PMT_SET_PNR(_psi, _pnr)                                                                 \
    {                                                                                           \
        const uint16_t __pnr = _pnr;                                                            \
        _psi->buffer[3] = __pnr >> 8;                                                           \
        _psi->buffer[4] = __pnr & 0xFF;                                                         \
    }

#define PMT_GET_PCR(_psi) (((_psi->buffer[8] & 0x1F) << 8) | _psi->buffer[9])
#define PMT_SET_PCR(_psi, _pcr)                                                                 \
    {                                                                                           \
        const uint16_t __pcr = _pcr;                                                            \
        _psi->buffer[8] = (__pcr >> 8) & 0x1F;                                                  \
        _psi->buffer[9] = __pcr & 0xFF;                                                         \
    }

#define PMT_GET_VERSION(_psi) PAT_GET_VERSION(_psi)
#define PMT_SET_VERSION(_psi, _version) PAT_SET_VERSION(_psi, _version)

#define PMT_DESC_FIRST(_psi) (&_psi->buffer[12])
#define __PMT_DESC_SIZE(_psi) (((_psi->buffer[10] & 0x0F) << 8) | _psi->buffer[11])
#define PMT_DESC_EOL(_psi, _desc_pointer) \
    (_desc_pointer >= (PMT_DESC_FIRST(_psi) + __PMT_DESC_SIZE(_psi)))
#define PMT_DESC_NEXT(_psi, _desc_pointer) _desc_pointer += 2 + _desc_pointer[1]

#define __PMT_ITEM_DESC_SIZE(_pointer) (((_pointer[3] & 0x0F) << 8) | _pointer[4])

#define PMT_ITEMS_FIRST(_psi) (PMT_DESC_FIRST(_psi) + __PMT_DESC_SIZE(_psi))
#define PMT_ITEMS_EOL(_psi, _pointer) PAT_ITEMS_EOL(_psi, _pointer)
#define PMT_ITEMS_NEXT(_psi, _pointer) _pointer += 5 + __PMT_ITEM_DESC_SIZE(_pointer)

#define PMT_ITEM_GET_TYPE(_psi, _pointer) _pointer[0]
#define PMT_ITEM_GET_PID(_psi, _pointer) (((_pointer[1] & 0x1F) << 8) | _pointer[2])

#define PMT_ITEM_DESC_FIRST(_pointer) (&_pointer[5])
#define PMT_ITEM_DESC_EOL(_pointer, _desc_pointer) \
    (_desc_pointer >= PMT_ITEM_DESC_FIRST(_pointer) + __PMT_ITEM_DESC_SIZE(_pointer))
#define PMT_ITEM_DESC_NEXT(_pointer, _desc_pointer) _desc_pointer += 2 + _desc_pointer[1]

/*
 *  oooooooo8 ooooooooo   ooooooooooo
 * 888         888    88o 88  888  88
 *  888oooooo  888    888     888
 *         888 888    888     888
 * o88oooo888 o888ooo88      o888o
 *
 */

#define SDT_GET_TSID(_psi) ((_psi->buffer[3] << 8) | _psi->buffer[4])
#define SDT_SET_TSID(_psi, _tsid)                                                               \
    {                                                                                           \
        const uint16_t __tsid = _tsid;                                                          \
        _psi->buffer[3] = __tsid >> 8;                                                          \
        _psi->buffer[4] = __tsid & 0xFF;                                                        \
    }

#define __SDT_ITEM_DESC_SIZE(_pointer) (((_pointer[3] & 0x0F) << 8) | _pointer[4])

#define SDT_ITEMS_FIRST(_psi) (&_psi->buffer[11])
#define SDT_ITEMS_EOL(_psi, _pointer)                                                           \
    ((_pointer - _psi->buffer) >= (_psi->buffer_size - CRC32_SIZE))
#define SDT_ITEMS_NEXT(_psi, _pointer) _pointer += 5 + __SDT_ITEM_DESC_SIZE(_pointer)

#define SDT_ITEM_GET_SID(_psi, _pointer) ((_pointer[0] << 8) | _pointer[1])
#define SDT_ITEM_DESC_FIRST(_pointer) (&_pointer[5])
#define SDT_ITEM_DESC_EOL(_pointer, _desc_pointer) \
    (_desc_pointer >= SDT_ITEM_DESC_FIRST(_pointer) + __SDT_ITEM_DESC_SIZE(_pointer))
#define SDT_ITEM_DESC_NEXT(_pointer, _desc_pointer) _desc_pointer += 2 + _desc_pointer[1]

#endif /* _MPEGTS_H_ */
