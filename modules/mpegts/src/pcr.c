/*
 * Astra Module: MPEG-TS (PCR)
 * http://cesbo.com/astra
 *
 * Copyright (C) 2014, Andrey Dyldin <and@cesbo.com>
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

#include "../mpegts.h"

inline bool mpegts_pcr_check(const uint8_t *ts)
{
    return (   (ts[0] == 0x47)
            && (ts[3] & 0x20)   /* adaptation field without payload */
            && (ts[4] > 0)      /* adaptation field length */
            && (ts[5] & 0x10)   /* PCR_flag */
            );
}

inline uint64_t mpegts_pcr(const uint8_t *ts)
{
    const uint64_t pcr_base = (ts[6] << 25)
                            | (ts[7] << 17)
                            | (ts[8] << 9 )
                            | (ts[9] << 1 )
                            | (ts[10] >> 7);
    const uint64_t pcr_ext = ((ts[10] & 1) << 8) | ts[11];
    return (pcr_base * 300 + pcr_ext);
}

inline double mpegts_pcr_block_ms(uint64_t pcr_last, uint64_t pcr_current)
{
    const uint64_t delta_pcr = pcr_current - pcr_last;
    const uint64_t dpcr_base = delta_pcr / 300;
    const uint64_t dpcr_ext = delta_pcr % 300;
    return (  (double)(dpcr_base / 90.0)        // 90 kHz
            + (double)(dpcr_ext / 27000.0));    // 27 MHz
}
