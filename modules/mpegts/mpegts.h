/*
 * Astra Module: MPEG-TS
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

#ifndef _MPEGTS_H_
#define _MPEGTS_H_ 1

#include <astra.h>

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
#define DESC_MAX_SIZE 1024

#define TS_IS_SYNC(_ts) (_ts[0] == 0x47)
#define TS_IS_PAYLOAD(_ts) (_ts[3] & 0x10)
#define TS_IS_PAYLOAD_START(_ts) (TS_IS_PAYLOAD(_ts) && (_ts[1] & 0x40))
#define TS_IS_AF(_ts) (_ts[3] & 0x20)
#define TS_IS_SCRAMBLED(_ts) (_ts[3] & 0xC0)

#define TS_GET_PID(_ts) ((uint16_t)(((_ts[1] & 0x1F) << 8) | _ts[2]))
#define TS_SET_PID(_ts, _pid)                                                                   \
    {                                                                                           \
        uint8_t *__ts = _ts;                                                                    \
        const uint16_t __pid = _pid;                                                            \
        __ts[1] = (__ts[1] & ~0x1F) | ((__pid >> 8) & 0x1F);                                    \
        __ts[2] = __pid & 0xFF;                                                                 \
    }

#define TS_GET_CC(_ts) (_ts[3] & 0x0F)
#define TS_SET_CC(_ts, _cc) { _ts[3] = (_ts[3] & 0xF0) | ((_cc) & 0x0F); }

#define TS_GET_PAYLOAD(_ts) (                                                                   \
    (!TS_IS_PAYLOAD(_ts)) ? (NULL) : (                                                          \
        (!TS_IS_AF(_ts)) ? (&_ts[TS_HEADER_SIZE]) : (                                           \
            (_ts[4] > TS_BODY_SIZE - 1) ? (NULL) : (&_ts[TS_HEADER_SIZE + 1 + _ts[4]]))         \
        )                                                                                       \
    )

typedef void (*ts_callback_t)(void *, const uint8_t *);

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
    MPEGTS_PACKET_DATA      = 0x01000000,
    MPEGTS_PACKET_NULL      = 0x02000000
} mpegts_packet_type_t;

const char * mpegts_type_name(mpegts_packet_type_t type);
mpegts_packet_type_t mpegts_pes_type(uint8_t type_id);

const char * mpeg4_profile_level_name(uint8_t type_id);

void mpegts_desc_to_lua(const uint8_t *desc);

/*
 * oooooooooo   oooooooo8 ooooo
 *  888    888 888         888
 *  888oooo88   888oooooo  888
 *  888                888 888
 * o888o       o88oooo888 o888o
 *
 */

#define PSI_MAX_SIZE 0x00000FFF

#define PSI_HEADER_SIZE 3

#define PSI_BUFFER_GET_SIZE(_b) (PSI_HEADER_SIZE + (((_b[1] & 0x0f) << 8) | _b[2]))

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

typedef void (*psi_callback_t)(void *, mpegts_psi_t *);

mpegts_psi_t * mpegts_psi_init(mpegts_packet_type_t type, uint16_t pid);
void mpegts_psi_destroy(mpegts_psi_t *psi);

void mpegts_psi_mux(mpegts_psi_t *psi, const uint8_t *ts, psi_callback_t callback, void *arg);
void mpegts_psi_demux(mpegts_psi_t *psi, ts_callback_t callback, void *arg);

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
        const uint16_t __size = _psi->buffer_size - PSI_HEADER_SIZE;                            \
        _psi->buffer[1] = (_psi->buffer[1] & 0xF0) | ((__size >> 8) & 0x0F);                    \
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

#define PES_MAX_SIZE 0x000A0000

#define PES_HEADER_SIZE 6

#define PES_BUFFER_GET_SIZE(_b) (((_b[4] << 8) | _b[5]) + 6)
#define PES_BUFFER_GET_HEADER(_b) ((_b[0] << 16) | (_b[1] << 8) | (_b[2]))

typedef struct
{
    mpegts_packet_type_t type;
    uint16_t pid;
    uint8_t cc;

    uint64_t block_time_begin;
    uint64_t block_time_total;

    // demux
    uint8_t ts[TS_PACKET_SIZE];

    uint32_t pcr_interval;
    uint64_t pcr_time;
    uint64_t pcr_time_offset;

    // mux
    uint32_t buffer_size;
    uint32_t buffer_skip;
    uint8_t buffer[PES_MAX_SIZE];
} mpegts_pes_t;

typedef void (*pes_callback_t)(void *, mpegts_pes_t *);

mpegts_pes_t * mpegts_pes_init(mpegts_packet_type_t type, uint16_t pid, uint32_t pcr_interval);
void mpegts_pes_destroy(mpegts_pes_t *pes);

void mpegts_pes_mux(mpegts_pes_t *pes, const uint8_t *ts, pes_callback_t callback, void *arg);
void mpegts_pes_demux(mpegts_pes_t *pes, ts_callback_t callback, void *arg);

#define PES_IS_SYNTAX_SPEC(_pes)                                                                \
    (                                                                                           \
        _pes->buffer[3] != 0xBC && /* program_stream_map */                                     \
        _pes->buffer[3] != 0xBE && /* padding_stream */                                         \
        _pes->buffer[3] != 0xBF && /* private_stream_2 */                                       \
        _pes->buffer[3] != 0xF0 && /* ECM */                                                    \
        _pes->buffer[3] != 0xF1 && /* EMM */                                                    \
        _pes->buffer[3] != 0xF2 && /* DSMCC_stream */                                           \
        _pes->buffer[3] != 0xF8 && /* ITU-T Rec. H.222.1 type E */                              \
        _pes->buffer[3] != 0xFF    /* program_stream_directory */                               \
    )

#define PES_INIT(_pes, _stream_id, _is_pts, _is_dts)                                            \
    {                                                                                           \
        const uint8_t __stream_id = _stream_id;                                                 \
        _pes->buffer[0] = 0x00;                                                                 \
        _pes->buffer[1] = 0x00;                                                                 \
        _pes->buffer[2] = 0x01;                                                                 \
        _pes->buffer[3] = __stream_id;                                                          \
        _pes->buffer[4] = 0x00;                                                                 \
        _pes->buffer[5] = 0x00;                                                                 \
        _pes->buffer_size = PES_HEADER_SIZE;                                                    \
        if(PES_IS_SYNTAX_SPEC(_pes))                                                            \
        {                                                                                       \
            _pes->buffer[6] = 0x80;                                                             \
            _pes->buffer[7] = 0x00;                                                             \
            _pes->buffer[8] = 0;                                                                \
            _pes->buffer_size += 3;                                                             \
            if(_is_pts)                                                                         \
            {                                                                                   \
                _pes->buffer[7] = _pes->buffer[7] | 0x80;                                       \
                _pes->buffer[8] += 5;                                                           \
                _pes->buffer_size += 5;                                                         \
                if(_is_dts)                                                                     \
                {                                                                               \
                    _pes->buffer[7] = _pes->buffer[7] | 0x40;                                   \
                    _pes->buffer[8] += 5;                                                       \
                    _pes->buffer_size += 5;                                                     \
                }                                                                               \
            }                                                                                   \
        }                                                                                       \
    }

#define __PES_IS_PTS(_pes) (PES_IS_SYNTAX_SPEC(_pes) && (_pes->buffer[7] & 0x80))

#define PES_GET_PTS(_pes)                                                                       \
    ((!__PES_IS_PTS(_pes)) ? (0) : (                                                            \
        (uint64_t)((_pes->buffer[9 ] & 0x0E) << 29) |                                           \
                  ((_pes->buffer[10]       ) << 22) |                                           \
                  ((_pes->buffer[11] & 0xFE) << 14) |                                           \
                  ((_pes->buffer[12]       ) << 7 ) |                                           \
                  ((_pes->buffer[13]       ) >> 1 )                                             \
    ))

#define PES_SET_PTS(_pes, _pts)                                                                 \
    {                                                                                           \
        asc_assert(__PES_IS_PTS(_pes), "PTS flag is not set");                                  \
        const uint64_t __pts = _pts;                                                            \
        _pes->buffer[9] = 0x20 | ((__pts >> 29) & 0x0E) | 0x01;                                 \
        _pes->buffer[10] = ((__pts >> 22) & 0xFF);                                              \
        _pes->buffer[11] = ((__pts >> 14) & 0xFE) | 0x01;                                       \
        _pes->buffer[12] = ((__pts >> 7 ) & 0xFF);                                              \
        _pes->buffer[13] = ((__pts << 1 ) & 0xFE) | 0x01;                                       \
    }

#define __PES_IS_DTS(_pes) (PES_IS_SYNTAX_SPEC(_pes) && (_pes->buffer[7] & 0x40))

#define PES_GET_DTS(_pes)                                                                       \
    ((!__PES_IS_DTS(_pes)) ? (0) : (                                                            \
        (uint64_t)((_pes->buffer[14] & 0x0E) << 29) |                                           \
                  ((_pes->buffer[15]       ) << 22) |                                           \
                  ((_pes->buffer[16] & 0xFE) << 14) |                                           \
                  ((_pes->buffer[17]       ) << 7 ) |                                           \
                  ((_pes->buffer[18]       ) >> 1 )                                             \
    ))

#define PES_SET_DTS(_pes, _dts)                                                                 \
    {                                                                                           \
        asc_assert(__PES_IS_DTS(_pes), "DTS flag is not set");                                  \
        const uint64_t __dts = _dts;                                                            \
        _pes->buffer[9] = _pes->buffer[9] | 0x10;                                               \
        _pes->buffer[14] = 0x10 | ((__dts >> 29) & 0x0E) | 0x01;                                \
        _pes->buffer[15] = ((__dts >> 22) & 0xFF);                                              \
        _pes->buffer[16] = ((__dts >> 14) & 0xFE) | 0x01;                                       \
        _pes->buffer[17] = ((__dts >> 7 ) & 0xFF);                                              \
        _pes->buffer[18] = ((__dts << 1 ) & 0xFE) | 0x01;                                       \
    }

#define PES_SET_SIZE(_pes)                                                                      \
    {                                                                                           \
        if(_pes->type != MPEGTS_PACKET_VIDEO)                                                   \
        {                                                                                       \
            const uint16_t __size = _pes->buffer_size - PES_HEADER_SIZE;                        \
            _pes->buffer[4] = (__size >> 8) & 0xFF;                                             \
            _pes->buffer[5] = (__size     ) & 0xFF;                                             \
        }                                                                                       \
        else                                                                                    \
        {                                                                                       \
            _pes->buffer[4] = 0x00;                                                             \
            _pes->buffer[5] = 0x00;                                                             \
        }                                                                                       \
    }

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
        _psi->buffer[0] = 0x00;                                                                 \
        _psi->buffer[1] = 0x80 | 0x30;                                                          \
        PAT_SET_TSID(_psi, _tsid);                                                              \
        _psi->buffer[5] = 0x01;                                                                 \
        PAT_SET_VERSION(_psi, _version);                                                        \
        _psi->buffer[6] = 0x00;                                                                 \
        _psi->buffer[7] = 0x00;                                                                 \
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
        _psi->buffer[5] = 0xC0 | (((_version) << 1) & 0x3E) | (_psi->buffer[4] & 0x01);         \
    }

#define PAT_ITEMS_FIRST(_psi) (&_psi->buffer[8])
#define PAT_ITEMS_EOL(_psi, _pointer)                                                           \
    ((_pointer - _psi->buffer) >= (_psi->buffer_size - CRC32_SIZE))
#define PAT_ITEMS_NEXT(_psi, _pointer) _pointer += 4

#define PAT_ITEMS_APPEND(_psi, _pnr, _pid)                                                      \
    {                                                                                           \
        uint8_t *const __pointer_a = &_psi->buffer[_psi->buffer_size - CRC32_SIZE];             \
        PAT_ITEM_SET_PNR(_psi, __pointer_a, _pnr);                                              \
        PAT_ITEM_SET_PID(_psi, __pointer_a, _pid);                                              \
        _psi->buffer_size += 4;                                                                 \
        PSI_SET_SIZE(_psi);                                                                     \
    }

#define PAT_ITEMS_FOREACH(_psi, _ptr)                                                           \
    for(_ptr = PAT_ITEMS_FIRST(_psi)                                                            \
        ; !PAT_ITEMS_EOL(_psi, _ptr)                                                            \
        ; PAT_ITEMS_NEXT(_psi, _ptr))

#define PAT_ITEM_GET_PNR(_psi, _pointer) ((_pointer[0] << 8) | _pointer[1])
#define PAT_ITEM_GET_PID(_psi, _pointer) (((_pointer[2] & 0x1F) << 8) | _pointer[3])

#define PAT_ITEM_SET_PNR(_psi, _pointer, _pnr)                                                  \
    {                                                                                           \
        uint8_t *const __pointer = _pointer;                                                    \
        const uint16_t __pnr = _pnr;                                                            \
        __pointer[0] = __pnr >> 8;                                                              \
        __pointer[1] = __pnr & 0xFF;                                                            \
    }

#define PAT_ITEM_SET_PID(_psi, _pointer, _pid)                                                  \
    {                                                                                           \
        uint8_t *const __pointer = _pointer;                                                    \
        const uint16_t __pid = _pid;                                                            \
        __pointer[2] = 0xE0 | ((__pid >> 8) & 0x1F);                                            \
        __pointer[3] = __pid & 0xFF;                                                            \
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

#define CAT_DESC_FOREACH(_psi, _ptr)                                                            \
    for(_ptr = CAT_DESC_FIRST(_psi)                                                             \
        ; !CAT_DESC_EOL(_psi, _ptr)                                                             \
        ; CAT_DESC_NEXT(_psi, _ptr))

/*
 * oooooooooo oooo     oooo ooooooooooo
 *  888    888 8888o   888  88  888  88
 *  888oooo88  88 888o8 88      888
 *  888        88  888  88      888
 * o888o      o88o  8  o88o    o888o
 *
 */

#define PMT_INIT(_psi, _pnr, _version, _pcr, _desc, _desc_size)                                 \
    {                                                                                           \
        _psi->buffer[0] = 0x02;                                                                 \
        _psi->buffer[1] = 0x80 | 0x30;                                                          \
        PMT_SET_PNR(_psi, _pnr);                                                                \
        _psi->buffer[5] = 0x01;                                                                 \
        PMT_SET_VERSION(_psi, _version);                                                        \
        _psi->buffer[6] = 0x00;                                                                 \
        _psi->buffer[7] = 0x00;                                                                 \
        PMT_SET_PCR(_psi, _pcr);                                                                \
        const uint16_t __desc_size = _desc_size;                                                \
        _psi->buffer[10] = 0xF0 | ((__desc_size >> 8) & 0x0F);                                  \
        _psi->buffer[11] = __desc_size & 0xFF;                                                  \
        if(__desc_size > 0)                                                                     \
        {                                                                                       \
            uint16_t __desc_skip = 0;                                                           \
            const uint8_t *const __desc = _desc;                                                \
            while(__desc_skip < __desc_size)                                                    \
            {                                                                                   \
                memcpy(&_psi->buffer[12 + __desc_skip]                                          \
                       , &__desc[__desc_skip]                                                   \
                       , 2 + __desc[__desc_skip + 1]);                                          \
            }                                                                                   \
        }                                                                                       \
        _psi->buffer_size = 12 + __desc_size + CRC32_SIZE;                                      \
        PSI_SET_SIZE(_psi);                                                                     \
    }

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
        _psi->buffer[8] = 0xE0 | ((__pcr >> 8) & 0x1F);                                         \
        _psi->buffer[9] = __pcr & 0xFF;                                                         \
    }

#define PMT_GET_VERSION(_psi) PAT_GET_VERSION(_psi)
#define PMT_SET_VERSION(_psi, _version) PAT_SET_VERSION(_psi, _version)

#define PMT_DESC_FIRST(_psi) (&_psi->buffer[12])
#define __PMT_DESC_SIZE(_psi) (((_psi->buffer[10] & 0x0F) << 8) | _psi->buffer[11])
#define PMT_DESC_EOL(_psi, _desc_pointer) \
    (_desc_pointer >= (PMT_DESC_FIRST(_psi) + __PMT_DESC_SIZE(_psi)))
#define PMT_DESC_NEXT(_psi, _desc_pointer) _desc_pointer += 2 + _desc_pointer[1]

#define PMT_DESC_FOREACH(_psi, _ptr)                                                            \
    for(_ptr = PMT_DESC_FIRST(_psi)                                                             \
        ; !PMT_DESC_EOL(_psi, _ptr)                                                             \
        ; PMT_DESC_NEXT(_psi, _ptr))

#define __PMT_ITEM_DESC_SIZE(_pointer) (((_pointer[3] & 0x0F) << 8) | _pointer[4])

#define PMT_ITEMS_FIRST(_psi) (PMT_DESC_FIRST(_psi) + __PMT_DESC_SIZE(_psi))
#define PMT_ITEMS_EOL(_psi, _pointer) PAT_ITEMS_EOL(_psi, _pointer)
#define PMT_ITEMS_NEXT(_psi, _pointer) _pointer += 5 + __PMT_ITEM_DESC_SIZE(_pointer)

#define PMT_ITEMS_APPEND(_psi, _type, _pid, _desc, _desc_size)                                  \
    {                                                                                           \
        uint8_t *const __pointer_a = &_psi->buffer[_psi->buffer_size - CRC32_SIZE];             \
        PMT_ITEM_SET_TYPE(_psi, __pointer_a, _type);                                            \
        PMT_ITEM_SET_PID(_psi, __pointer_a, _pid);                                              \
        const uint16_t __desc_size = _desc_size;                                                \
        __pointer_a[3] = 0xF0 | ((__desc_size >> 8) & 0x0F);                                    \
        __pointer_a[4] = __desc_size & 0xFF;                                                    \
        _psi->buffer_size += (__desc_size + 5);                                                 \
        PSI_SET_SIZE(_psi);                                                                     \
    }

#define PMT_ITEMS_FOREACH(_psi, _ptr)                                                           \
    for(_ptr = PMT_ITEMS_FIRST(_psi)                                                            \
        ; !PMT_ITEMS_EOL(_psi, _ptr)                                                            \
        ; PMT_ITEMS_NEXT(_psi, _ptr))

#define PMT_ITEM_GET_TYPE(_psi, _pointer) _pointer[0]
#define PMT_ITEM_SET_TYPE(_psi, _pointer, _type) _pointer[0] = _type

#define PMT_ITEM_DESC_FIRST(_pointer) (&_pointer[5])
#define PMT_ITEM_DESC_EOL(_pointer, _desc_pointer) \
    (_desc_pointer >= PMT_ITEM_DESC_FIRST(_pointer) + __PMT_ITEM_DESC_SIZE(_pointer))
#define PMT_ITEM_DESC_NEXT(_pointer, _desc_pointer) _desc_pointer += 2 + _desc_pointer[1]

#define PMT_ITEM_DESC_FOREACH(_ptr, _desc_ptr)                                                  \
    for(_desc_ptr = PMT_ITEM_DESC_FIRST(_ptr)                                                   \
        ; !PMT_ITEM_DESC_EOL(_ptr, _desc_ptr)                                                   \
        ; PMT_ITEM_DESC_NEXT(_ptr, _desc_ptr))

#define PMT_ITEM_GET_PID(_psi, _pointer) (((_pointer[1] & 0x1F) << 8) | _pointer[2])
#define PMT_ITEM_SET_PID(_psi, _pointer, _pid)                                                  \
    {                                                                                           \
        uint8_t *const __pointer = _pointer;                                                    \
        const uint16_t __pid = _pid;                                                            \
        __pointer[1] = 0xE0 | ((__pid >> 8) & 0x1F);                                            \
        __pointer[2] = __pid & 0xFF;                                                            \
    }

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

#define SDT_GET_SECTION_NUMBER(_psi) (_psi->buffer[6])
#define SDT_SET_SECTION_NUMBER(_psi, _id) _psi->buffer[6] = _id

#define SDT_GET_LAST_SECTION_NUMBER(_psi) (_psi->buffer[7])
#define SDT_SET_LAST_SECTION_NUMBER(_psi, _id) _psi->buffer[7] = _id

#define __SDT_ITEM_DESC_SIZE(_pointer) (((_pointer[3] & 0x0F) << 8) | _pointer[4])

#define SDT_ITEMS_FIRST(_psi) (&_psi->buffer[11])
#define SDT_ITEMS_EOL(_psi, _pointer)                                                           \
    ((_pointer - _psi->buffer) >= (_psi->buffer_size - CRC32_SIZE))
#define SDT_ITEMS_NEXT(_psi, _pointer) _pointer += 5 + __SDT_ITEM_DESC_SIZE(_pointer)

#define SDT_ITEMS_FOREACH(_psi, _ptr)                                                           \
    for(_ptr = SDT_ITEMS_FIRST(_psi)                                                            \
        ; !SDT_ITEMS_EOL(_psi, _ptr)                                                            \
        ; SDT_ITEMS_NEXT(_psi, _ptr))

#define SDT_ITEM_GET_SID(_psi, _pointer) ((_pointer[0] << 8) | _pointer[1])
#define SDT_ITEM_SET_SID(_psi, _pointer, _sid)                                                  \
    {                                                                                           \
        const uint16_t __sid = _sid;                                                            \
        _pointer[0] = __sid >> 8;                                                               \
        _pointer[1] = __sid & 0xFF;                                                             \
    }

#define SDT_ITEM_DESC_FIRST(_pointer) (&_pointer[5])
#define SDT_ITEM_DESC_EOL(_pointer, _desc_pointer)                                              \
    (_desc_pointer >= SDT_ITEM_DESC_FIRST(_pointer) + __SDT_ITEM_DESC_SIZE(_pointer))
#define SDT_ITEM_DESC_NEXT(_pointer, _desc_pointer) _desc_pointer += 2 + _desc_pointer[1]

#define SDT_ITEM_DESC_FOREACH(_ptr, _desc_ptr)                                                  \
    for(_desc_ptr = SDT_ITEM_DESC_FIRST(_ptr)                                                   \
        ; !SDT_ITEM_DESC_EOL(_ptr, _desc_ptr)                                                   \
        ; SDT_ITEM_DESC_NEXT(_ptr, _desc_ptr))

/*
 * ooooooooooo ooooo ooooooooooo
 *  888    88   888  88  888  88
 *  888ooo8     888      888
 *  888    oo   888      888
 * o888ooo8888 o888o    o888o
 *
 */

#define EIT_GET_PNR(_psi) ((_psi->buffer[3] << 8) | _psi->buffer[4])
#define EIT_SET_PNR(_psi, _pnr)                                                                 \
    {                                                                                           \
        const uint16_t __pnr = _pnr;                                                            \
        _psi->buffer[3] = __pnr >> 8;                                                           \
        _psi->buffer[4] = __pnr & 0xFF;                                                         \
    }

#define EIT_GET_TSID(_psi) ((_psi->buffer[8] << 8) | _psi->buffer[9])

/*
 * oooooooooo    oooooooo8 oooooooooo
 *  888    888 o888     88  888    888
 *  888oooo88  888          888oooo88
 *  888        888o     oo  888  88o
 * o888o        888oooo88  o888o  88o8
 *
 */

#define TS_IS_PCR(_ts)                                                                          \
    (                                                                                           \
        (_ts[0] == 0x47) &&                                                                     \
        (TS_IS_AF(_ts)) &&              /* adaptation field */                                  \
        (_ts[4] > 0) &&                 /* adaptation field length */                           \
        (_ts[5] & 0x10)                 /* PCR_flag */                                          \
    )

#define TS_GET_PCR(_ts)                                                                         \
    ((uint64_t)(                                                                                \
        300 * (((_ts)[6] << 25)  |                                                              \
               ((_ts)[7] << 17)  |                                                              \
               ((_ts)[8] << 9 )  |                                                              \
               ((_ts)[9] << 1 )  |                                                              \
               ((_ts)[10] >> 7)) +                                                              \
        ((((_ts)[10] & 0x01) << 8) | (_ts)[11])                                                 \
    ))

#define TS_SET_PCR(_ts, _pcr)                                                                   \
    {                                                                                           \
        uint8_t *const __ts = _ts;                                                              \
        const uint64_t __pcr = _pcr;                                                            \
        const uint64_t __pcr_base = __pcr / 300;                                                \
        const uint64_t __pcr_ext = __pcr % 300;                                                 \
        __ts[6] = (__pcr_base >> 25) & 0xFF;                                                    \
        __ts[7] = (__pcr_base >> 17) & 0xFF;                                                    \
        __ts[8] = (__pcr_base >> 9 ) & 0xFF;                                                    \
        __ts[9] = (__pcr_base >> 1 ) & 0xFF;                                                    \
        __ts[10] = ((__pcr_base << 7) & 0x80) | 0x7E | ((__pcr_ext >> 8) & 0x01);               \
        __ts[11] = __pcr_ext & 0xFF;                                                            \
    }

uint64_t mpegts_pcr_block_us(uint64_t *pcr_last, const uint64_t *pcr_current);

#endif /* _MPEGTS_H_ */
