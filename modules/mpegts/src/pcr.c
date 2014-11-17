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

inline uint64_t mpegts_pcr_block_us(uint64_t *pcr_last, const uint64_t *pcr_current)
{
    if(*pcr_current <= *pcr_last)
    {
        *pcr_last = *pcr_current;
        return 0;
    }

    const uint64_t delta_pcr = *pcr_current - *pcr_last;
    *pcr_last = *pcr_current;
    const uint64_t dpcr_base = delta_pcr / 300;
    const uint64_t dpcr_ext = delta_pcr % 300;
    return (dpcr_base * 1000 / 90) + (dpcr_ext * 1000 / 27000);
}
