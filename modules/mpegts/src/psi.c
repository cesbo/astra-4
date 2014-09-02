/*
 * Astra Module: MPEG-TS (PSI processing)
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
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

#include <stdlib.h>
#include <string.h>
#include "../mpegts.h"

mpegts_psi_t * mpegts_psi_init(mpegts_packet_type_t type, uint16_t pid)
{
    mpegts_psi_t *psi = malloc(sizeof(mpegts_psi_t));
    psi->type = type;
    psi->pid = pid;
    psi->cc = 0;
    psi->buffer_size = 0;
    psi->buffer_skip = 0;
    psi->crc32 = 0;
    return psi;
}

void mpegts_psi_destroy(mpegts_psi_t *psi)
{
    if(!psi)
        return;

    free(psi);
}

void mpegts_psi_mux(mpegts_psi_t *psi, const uint8_t *ts, psi_callback_t callback, void *arg)
{
    const uint8_t *payload;
    switch(TS_AF(ts))
    {
        case 0x10:
            payload = &ts[TS_HEADER_SIZE];
            break;
        case 0x30:
            if(ts[4] >= TS_BODY_SIZE - 1)
                return;
            payload = &ts[TS_HEADER_SIZE + 1 + ts[4]];
            break;
        default:
            return;
    }

    const uint8_t cc = TS_CC(ts);

    if(TS_PUSI(ts))
    {
        const uint8_t ptr_field = *payload;
        ++payload; // skip pointer field

        if(ptr_field > 0)
        { // pointer field
            if(ptr_field >= TS_BODY_SIZE)
            {
                psi->buffer_skip = 0;
                return;
            }
            if(psi->buffer_skip > 0)
            {
                if(((psi->cc + 1) & 0x0f) != cc)
                { // discontinuity error
                    psi->buffer_skip = 0;
                    return;
                }
                memcpy(&psi->buffer[psi->buffer_skip], payload, ptr_field);
                if(psi->buffer_size == 0)
                { // incomplete PSI header
                    const size_t psi_buffer_size = PSI_BUFFER_GET_SIZE(psi->buffer);
                    if(psi_buffer_size <= 3 || psi_buffer_size > PSI_MAX_SIZE)
                    {
                        psi->buffer_skip = 0;
                        return;
                    }
                    psi->buffer_size = psi_buffer_size;
                }
                if(psi->buffer_size != psi->buffer_skip + ptr_field)
                { // checking PSI length
                    psi->buffer_skip = 0;
                    return;
                }
                psi->buffer_skip = 0;
                callback(arg, psi);
            }
            payload += ptr_field;
        }
        while(((payload - ts) < TS_PACKET_SIZE) && (payload[0] != 0xff))
        {
            psi->buffer_size = 0;

            const uint8_t remain = (ts + TS_PACKET_SIZE) - payload;
            if(remain < 3)
            {
                memcpy(psi->buffer, payload, remain);
                psi->buffer_skip = remain;
                break;
            }

            const size_t psi_buffer_size = PSI_BUFFER_GET_SIZE(payload);
            if(psi_buffer_size <= 3 || psi_buffer_size > PSI_MAX_SIZE)
                break;

            const size_t cpy_len = (ts + TS_PACKET_SIZE) - payload;
            if(cpy_len > TS_BODY_SIZE)
                break;

            psi->buffer_size = psi_buffer_size;
            if(psi_buffer_size > cpy_len)
            {
                memcpy(psi->buffer, payload, cpy_len);
                psi->buffer_skip = cpy_len;
                break;
            }
            else
            {
                memcpy(psi->buffer, payload, psi_buffer_size);
                psi->buffer_skip = 0;
                callback(arg, psi);
                payload += psi_buffer_size;
            }
        }
    }
    else
    { // !TS_PUSI(ts)
        if(!psi->buffer_skip)
            return;
        if(((psi->cc + 1) & 0x0f) != cc)
        { // discontinuity error
            psi->buffer_skip = 0;
            return;
        }
        if(psi->buffer_size == 0)
        { // incomplete PSI header
            if(psi->buffer_skip >= 3)
            {
                psi->buffer_skip = 0;
                return;
            }
            memcpy(&psi->buffer[psi->buffer_skip], payload, 3 - psi->buffer_skip);
            const size_t psi_buffer_size = PSI_BUFFER_GET_SIZE(psi->buffer);
            if(psi_buffer_size <= 3 || psi_buffer_size > PSI_MAX_SIZE)
            {
                psi->buffer_skip = 0;
                return;
            }
            psi->buffer_size = psi_buffer_size;
        }
        const size_t remain = psi->buffer_size - psi->buffer_skip;
        if(remain <= TS_BODY_SIZE)
        {
            memcpy(&psi->buffer[psi->buffer_skip], payload, remain);
            psi->buffer_skip = 0;
            callback(arg, psi);
        }
        else
        {
            memcpy(&psi->buffer[psi->buffer_skip], payload, TS_BODY_SIZE);
            psi->buffer_skip += TS_BODY_SIZE;
        }
    }
    psi->cc = cc;
} /* mpegts_psi_mux */

void mpegts_psi_demux(mpegts_psi_t *psi, ts_callback_t callback, void *arg)
{
    const size_t buffer_size = psi->buffer_size;
    if(!buffer_size)
        return;

    uint8_t *ts = psi->ts;

    ts[0] = 0x47;
    ts[1] = 0x40 /* PUSI */ | psi->pid >> 8;
    ts[2] = psi->pid & 0xff;
    ts[4] = 0x00;

    const uint8_t ts_3 = 0x10; /* payload without adaptation field */

    // 1 - pointer field
    size_t ts_skip = TS_HEADER_SIZE + 1;
    size_t ts_size = TS_BODY_SIZE - 1;
    size_t buffer_skip = 0;

    while(buffer_skip < buffer_size)
    {
        const size_t buffer_tail = buffer_size - buffer_skip;
        if(buffer_tail < ts_size)
        {
            ts_size = buffer_tail;
            const size_t ts_last_byte = ts_skip + ts_size;
            memset(&ts[ts_last_byte], 0xFF, TS_PACKET_SIZE - ts_last_byte);
        }

        memcpy(&ts[ts_skip], &psi->buffer[buffer_skip], ts_size);
        ts[3] = ts_3 | psi->cc;

        buffer_skip += ts_size;
        psi->cc = (psi->cc + 1) & 0x0F;

        callback(arg, ts);

        if(ts_skip == 5)
        {
            ts_skip = TS_HEADER_SIZE;
            ts_size = TS_BODY_SIZE;
            ts[1] &= ~0x40; /* turn off pusi bit */
        }
    }
} /* mpegts_packet_demux */
