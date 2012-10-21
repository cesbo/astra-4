/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#include <astra.h>
#include "../mpegts.h"

mpegts_psi_t * mpegts_pat_init(void)
{
    mpegts_psi_t *psi = mpegts_psi_init(MPEGTS_PACKET_PAT, 0);
    mpegts_pat_t *pat = calloc(1, sizeof(mpegts_pat_t));
    psi->data = pat;
    pat->current_next = 1;
    return psi;
}

void mpegts_pat_destroy(mpegts_psi_t *psi)
{
    if(!psi)
        return;

    mpegts_pat_t *pat = psi->data;

    list_t *i = pat->items;
    while(i)
    {
        free(list_get_data(i));
        i = list_delete(i, NULL);
    }

    free(pat);
    psi->data = NULL;
    mpegts_psi_destroy(psi);
}

void mpegts_pat_parse(mpegts_psi_t *psi)
{
    mpegts_pat_t *pat = psi->data;

    const uint32_t curr_crc = mpegts_psi_get_crc(psi);
    if(psi->crc32 == curr_crc)
    {
        psi->status = MPEGTS_UNCHANGED;
        return;
    }
    const uint32_t calc_crc = mpegts_psi_calc_crc(psi);

    uint8_t *buffer = psi->buffer;
    uint8_t * const buffer_end = buffer + psi->buffer_size - CRC32_SIZE;

    do
    {
        if(psi->type != MPEGTS_PACKET_PAT)
        {
            psi->status = MPEGTS_ERROR_PACKET_TYPE;
            break;
        }
        if(buffer[0] != 0x00) // check Table ID
        {
            psi->status = MPEGTS_ERROR_TABLE_ID;
            break;
        }
        if((buffer[1] & 0xCC) != 0x80) // check fixed bits
        {
            psi->status = MPEGTS_ERROR_FIXED_BITS;
            break;
        }
        if(psi->buffer_size > 1024) // check section length
        {
            psi->status = MPEGTS_ERROR_LENGTH;
            break;
        }
        if(curr_crc != calc_crc)
        {
            psi->status = MPEGTS_ERROR_CRC32;
            break;
        }
        if(pat->items)
        {
            psi->status = MPEGTS_CRC32_CHANGED;
            return;
        }

        psi->status = MPEGTS_ERROR_NONE;
        psi->crc32 = curr_crc;
    } while(0);

    if(psi->status != MPEGTS_ERROR_NONE)
    {
        log_error("PAT: error %d [0x%02X%02X size:%d crc:0x%08X]", psi->status
                  , buffer[0], buffer[1], psi->buffer_size, calc_crc);
        return;
    }

    pat->stream_id = (buffer[3] << 8) | buffer[4];
    pat->version = (buffer[5] & 0x3E) >> 1;
    pat->current_next = buffer[5] & 0x01;

    buffer += 8; // skip PAT header

    while(buffer < buffer_end)
    {
        const uint16_t pnr = (buffer[0] << 8) | buffer[1];
        const uint16_t ppid = ((buffer[2] & 0x1f) << 8) | buffer[3];

        mpegts_pat_item_add(psi, ppid, pnr);

        buffer += 4; // 4 - pnr + ppid
    }
} /* mpegts_pat_parse */

void mpegts_pat_dump(mpegts_psi_t *psi, const char *name)
{
    mpegts_pat_t *pat = psi->data;

    log_info("[PAT %s] stream_id:%d", name, pat->stream_id);

    list_t *i = list_get_first(pat->items);
    while(i)
    {
        mpegts_pat_item_t *item = list_get_data(i);
        if(item->pnr == 0)
            log_info("[PAT %s] pid:%4d NIT", name, item->pid);
        else
            log_info("[PAT %s] pid:%4d PMT pnr:%d", name, item->pid, item->pnr);
        i = list_get_next(i);
    }
    log_info("[PAT %s] crc:0x%08X", name, psi->crc32);
} /* mpegts_pat_dump */

void mpegts_pat_assemble(mpegts_psi_t *psi)
{
    mpegts_pat_t *pat = psi->data;

    uint8_t *buffer = psi->buffer;
    buffer[0] = 0x00; // table id
    buffer[1] = 0x80 | 0x30; // section syntax indicator | reserved
    const uint16_t stream_id = pat->stream_id;
    buffer[3] = stream_id >> 8;
    buffer[4] = stream_id & 0xFF;
    buffer[5] = ((pat->version << 1) & 0x3E) | (pat->current_next & 1);
    buffer[6] = buffer[7] = 0x00; // section number and last section number

    uint8_t *ptr = &buffer[8]; // skip PAT header
    list_t *i = list_get_first(pat->items);
    while(i)
    {
        mpegts_pat_item_t *item = list_get_data(i);
        const uint16_t pnr = item->pnr;
        ptr[0] = pnr >> 8;
        ptr[1] = pnr & 0xFF;
        const uint16_t pid = item->pid;
        ptr[2] = (pid >> 8) & 0x1F;
        ptr[3] = pid & 0xFF;
        ptr += 4; // 4 - pnr + pid
        i = list_get_next(i);
    }

    // 3 - PSI header (before section length)
    const uint16_t slen = (ptr - buffer - 3 + CRC32_SIZE);
    buffer[1] |= ((slen >> 8) & 0x0F);
    buffer[2] = slen & 0xFF;
    psi->buffer_size = slen + 3;

    const uint32_t crc = mpegts_psi_calc_crc(psi);
    ptr[0] = crc >> 24;
    ptr[1] = crc >> 16;
    ptr[2] = crc >> 8;
    ptr[3] = crc;
} /* mpegts_pat_assemble */

void mpegts_pat_item_add(mpegts_psi_t *psi, uint16_t pid, uint16_t pnr)
{
    mpegts_pat_item_t *item = calloc(1, sizeof(mpegts_pat_item_t));
    item->pid = pid;
    item->pnr = pnr;

    // TODO: sort PAT items

    mpegts_pat_t *pat = psi->data;
    pat->items = list_append(pat->items, item);
}

void mpegts_pat_item_delete(mpegts_psi_t *psi, uint16_t pid)
{
    mpegts_pat_t *pat = psi->data;
    list_t *i = list_get_first(pat->items);
    while(i)
    {
        mpegts_pat_item_t *item = list_get_data(i);
        if(item->pid == pid)
        {
            free(item);
            if(i == pat->items)
                pat->items = list_delete(i, NULL);
            else
                list_delete(i, NULL);
            return;
        }
        i = list_get_next(i);
    }
}
