/*
 * Astra Module: MPEG-TS (PES processing)
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

#include <core/log.h>

mpegts_pes_t * mpegts_pes_init(mpegts_packet_type_t type, uint16_t pid)
{
    mpegts_pes_t *pes = malloc(sizeof(mpegts_pes_t));
    pes->type = type;
    pes->pid = pid;
    pes->cc = 0;
    pes->pts = 0;
    pes->dts = 0;
    pes->pcr = 0;
    pes->buffer_size = 0;
    pes->buffer_skip = 0;
    return pes;
}

void mpegts_pes_destroy(mpegts_pes_t *pes)
{
    if(!pes)
        return;

    free(pes);
}

void mpegts_pes_mux(mpegts_pes_t *pes, const uint8_t *ts, pes_callback_t callback, void *arg)
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
    const uint8_t payload_len = ts + TS_PACKET_SIZE - payload;
    const uint8_t cc = TS_CC(ts);

    if(TS_PUSI(ts))
    {
        if(pes->buffer_skip > 0)
        {
            pes->buffer_size = pes->buffer_skip;
            pes->buffer_skip = 0;
            callback(arg, pes);
        }

        if(payload_len < PES_HEADER_SIZE)
            return;

        if(PES_BUFFER_GET_HEADER(payload) != 0x000001)
            return;

        pes->buffer_size = PES_BUFFER_GET_SIZE(payload);

        memcpy(pes->buffer, payload, payload_len);
        pes->buffer_skip = payload_len;

        if(pes->buffer_size == pes->buffer_skip)
        {
            pes->buffer_skip = 0;
            callback(arg, pes);
        }
    }
    else
    { // !TS_PUSI(ts)
        if(!pes->buffer_skip)
            return;

        if(((pes->cc + 1) & 0x0f) != cc)
        { // discontinuity error
            pes->buffer_skip = 0;
            return;
        }

        memcpy(&pes->buffer[pes->buffer_skip], payload, payload_len);
        pes->buffer_skip += payload_len;

        if(pes->buffer_size == pes->buffer_skip)
        {
            pes->buffer_skip = 0;
            callback(arg, pes);
        }
    }
    pes->cc = cc;
}

void mpegts_pes_demux(mpegts_pes_t *pes, ts_callback_t callback, void *arg)
{
    if(pes->buffer_size == 0)
        return;

    PES_SET_SIZE(pes);

    pes->ts[0] = 0x47;
    pes->ts[1] = 0x40; /* PUSI */
    TS_SET_PID(pes->ts, pes->pid);
    pes->ts[3] = 0x10; /* payload only */

    size_t buffer_skip = 0;

    do
    {
        const size_t buffer_tail = pes->buffer_size - buffer_skip;

        if(buffer_tail >= TS_BODY_SIZE)
        {
            memcpy(&pes->ts[TS_HEADER_SIZE], &pes->buffer[buffer_skip], TS_BODY_SIZE);
            buffer_skip += TS_BODY_SIZE;
        }
        else
        {
            const uint8_t stuff_size = TS_BODY_SIZE - buffer_tail - 2;
            pes->ts[3] = pes->ts[3] | 0x20; /* adaptation field */
            pes->ts[4] = 1 + stuff_size; /* 1 - ts[5] */
            pes->ts[5] = 0x00;
            memset(&pes->ts[6], 0xFF, stuff_size);
            memcpy(&pes->ts[6 + stuff_size], &pes->buffer[buffer_skip], buffer_tail);
            buffer_skip += buffer_tail;
        }

        pes->ts[3] = (pes->ts[3] & 0xF0) | (pes->cc & 0x0F);
        pes->cc = (pes->cc + 1) & 0x0F;

        callback(arg, pes->ts);

        if(TS_PUSI(pes->ts))
            pes->ts[1] = pes->ts[1] & ~0x40; /* unset PUSI */
    } while(buffer_skip != pes->buffer_size);
}

void mpegts_pes_demux_pcr(mpegts_pes_t *pes, ts_callback_t callback, void *arg)
{
    pes->ts[0] = 0x47;
    pes->ts[1] = 0x00;
    TS_SET_PID(pes->ts, pes->pid);
    pes->ts[3] = 0x20; /* adaptation field only */
    pes->ts[4] = 1 + 6; /* 1 - ts[5]; 6 - PCR field size */
    pes->ts[5] = 0x10; /* PCR flag */

    const uint64_t pcr_base = pes->pcr / 300;
    const uint64_t pcr_ext = pes->pcr % 300;
    pes->ts[6] = (pcr_base >> 25) & 0xFF;
    pes->ts[7] = (pcr_base >> 17) & 0xFF;
    pes->ts[8] = (pcr_base >> 9 ) & 0xFF;
    pes->ts[9] = (pcr_base >> 1 ) & 0xFF;
    pes->ts[10] = ((pcr_base << 7 ) & 0x80) | 0x7E | ((pcr_ext >> 8) & 0x01);
    pes->ts[11] = pcr_ext & 0xFF;

    memset(&pes->ts[12], 0xFF, TS_PACKET_SIZE - 12);

    callback(arg, pes->ts);
}

/* deprecated */
void mpegts_pes_add_data(mpegts_pes_t *pes, const uint8_t *data, uint32_t size)
{
    if(!pes->buffer_size)
    {
        uint8_t *b = pes->buffer;
        // PES header
        b[0] = 0x00;
        b[1] = 0x00;
        b[2] = 0x01;
        //
        b[3] = pes->stream_id;
        //
        b[6] = 0x00;
        b[7] = 0x00;
        // PES header length
        b[8] = 0;
        pes->buffer_size = 9;

        if(pes->pts)
        {
            b[7] |= 0x80;
            pes->buffer_size += 5;
        }
    }

    const size_t nsize = pes->buffer_size + size;
    if(nsize > PES_MAX_SIZE)
        return;

    memcpy(&pes->buffer[pes->buffer_size], data, size);
    pes->buffer_size = nsize;
}
