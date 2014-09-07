/*
 * Astra Module: MPEG-TS (PES processing)
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

#include "../mpegts.h"

mpegts_pes_t * mpegts_pes_init(mpegts_packet_type_t type, uint16_t pid, uint32_t pcr_interval)
{
    mpegts_pes_t *pes = malloc(sizeof(mpegts_pes_t));
    pes->type = type;
    pes->pid = pid;
    pes->cc = 0;
    pes->buffer_size = 0;
    pes->buffer_skip = 0;

    pes->pcr_interval = pcr_interval * 1000;
    pes->pcr_time = 0;
    pes->pcr_time_offset = 0;

    pes->ts[0] = 0x47;
    pes->ts[1] = 0x00;
    TS_SET_PID(pes->ts, pes->pid);

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
            pes->block_time_total = asc_utime() - pes->block_time_begin;
            callback(arg, pes);
        }

        if(payload_len < PES_HEADER_SIZE)
            return;

        if(PES_BUFFER_GET_HEADER(payload) != 0x000001)
            return;

        pes->buffer_size = PES_BUFFER_GET_SIZE(payload);
        pes->block_time_begin = asc_utime();

        memcpy(pes->buffer, payload, payload_len);
        pes->buffer_skip = payload_len;

        if(pes->buffer_size == pes->buffer_skip)
        {
            pes->buffer_skip = 0;
            pes->block_time_total = 0;
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
            pes->block_time_total = asc_utime() - pes->block_time_begin;
            callback(arg, pes);
        }
    }
    pes->cc = cc;
}

static inline bool check_pcr_time(mpegts_pes_t *pes)
{
    const uint64_t offset = (pes->buffer_skip * pes->block_time_total) / (pes->buffer_size);
    const uint64_t block_time_offset = pes->pcr_time_offset + pes->block_time_begin + offset;
    const uint64_t pcr_time_next = pes->pcr_time + pes->pcr_interval;
    if(block_time_offset >= pcr_time_next)
    {
        pes->pcr_time = pcr_time_next;
        pes->pcr_time_offset = block_time_offset - pcr_time_next;
        return true;
    }

    return false;
}

void mpegts_pes_demux(mpegts_pes_t *pes, ts_callback_t callback, void *arg)
{
    if(pes->buffer_size == 0)
        return;

    pes->buffer_skip = 0;
    pes->cc = pes->cc & 0x0F; /* check cc */

    do
    {
        if(pes->pcr_interval && check_pcr_time(pes))
        {
            pes->ts[1] = pes->ts[1] & ~0x40; /* unset PUSI */
            pes->ts[3] = 0x20 | pes->cc; /* adaptation field only */
            pes->ts[4] = 1 + 6 + 176; /* 1 - ts[5]; 6 - PCR field; 176 - stuff */
            pes->ts[5] = 0x10; /* PCR flag */

            const uint64_t pcr_base = pes->pcr_time * 90 / 1000;
            const uint64_t pcr_ext = pes->pcr_time * 27000 / 1000;
            const uint64_t pcr = (pcr_base * 300) + (pcr_ext % 300);
            PCR_SET(pes->ts, pcr);
            memset(&pes->ts[12], 0xFF, TS_PACKET_SIZE - 12);

            callback(arg, pes->ts);
        }

        if(pes->buffer_skip == 0)
            pes->ts[1] = pes->ts[1] | 0x40; /* set PUSI */

        const size_t buffer_tail = pes->buffer_size - pes->buffer_skip;

        if(buffer_tail >= TS_BODY_SIZE)
        {
            pes->ts[3] = 0x10 | pes->cc; /* payload only */
            memcpy(&pes->ts[TS_HEADER_SIZE], &pes->buffer[pes->buffer_skip], TS_BODY_SIZE);
            pes->buffer_skip += TS_BODY_SIZE;
        }
        else if(buffer_tail >= TS_BODY_SIZE - 2) /* 2 - adaptation field */
        {
            pes->ts[3] = 0x30 | pes->cc; /* payload with adaptation field */
            pes->ts[4] = 1;
            pes->ts[5] = 0x00;
            memcpy(&pes->ts[6], &pes->buffer[pes->buffer_skip], TS_BODY_SIZE - 2);
            pes->buffer_skip += TS_BODY_SIZE - 2;
        }
        else
        {
            const uint8_t stuff_size = TS_BODY_SIZE - buffer_tail - 2;
            pes->ts[3] = 0x30 | pes->cc; /* payload with adaptation field */
            pes->ts[4] = 1 + stuff_size; /* 1 - ts[5] */
            pes->ts[5] = 0x00;
            memset(&pes->ts[6], 0xFF, stuff_size);
            memcpy(&pes->ts[6 + stuff_size], &pes->buffer[pes->buffer_skip], buffer_tail);
            pes->buffer_skip += buffer_tail;
        }

        callback(arg, pes->ts);

        pes->cc = (pes->cc + 1) & 0x0F;
        if(TS_PUSI(pes->ts))
            pes->ts[1] = pes->ts[1] & ~0x40; /* unset PUSI */
    } while(pes->buffer_skip != pes->buffer_size);
}
