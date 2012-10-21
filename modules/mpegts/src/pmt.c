/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#include <astra.h>
#include "../mpegts.h"

mpegts_psi_t * mpegts_pmt_init(uint16_t pid)
{
    mpegts_psi_t *psi = mpegts_psi_init(MPEGTS_PACKET_PMT, pid);
    mpegts_pmt_t *pmt = calloc(1, sizeof(mpegts_pmt_t));
    psi->data = pmt;
    pmt->current_next = 1;
    return psi;
}

mpegts_psi_t * mpegts_pmt_duplicate(mpegts_psi_t *psi)
{
    if(!psi->data || psi->type != MPEGTS_PACKET_PMT)
        return NULL;

    mpegts_psi_t *npsi = mpegts_pmt_init(psi->pid);
    memcpy(npsi->buffer, psi->buffer, psi->buffer_size);
    npsi->buffer_size = psi->buffer_size;
    mpegts_pmt_parse(npsi);

    return npsi;
}

void mpegts_pmt_destroy(mpegts_psi_t *psi)
{
    if(!psi)
        return;

    mpegts_pmt_t *pmt = psi->data;

    mpegts_desc_destroy(pmt->desc);
    list_t *i = pmt->items;
    while(i)
    {
        mpegts_pmt_item_t *item = list_get_data(i);
        mpegts_desc_destroy(item->desc);
        free(item);
        i = list_delete(i, NULL);
    }

    free(pmt);
    psi->data = NULL;
    mpegts_psi_destroy(psi);
}

void mpegts_pmt_parse(mpegts_psi_t *psi)
{
    mpegts_pmt_t *pmt = psi->data;

    uint8_t *buffer = psi->buffer;
    uint8_t * const buffer_end = buffer + psi->buffer_size - CRC32_SIZE;

    const uint16_t pnr = ((buffer[3] << 8) | buffer[4]);
    if(pmt->pnr && pmt->pnr != pnr)
    {
        psi->status = MPEGTS_UNCHANGED;
        return;
    }

    const uint32_t curr_crc = mpegts_psi_get_crc(psi);
    if(psi->crc32 == curr_crc)
    {
        psi->status = MPEGTS_UNCHANGED;
        return;
    }
    const uint32_t calc_crc = mpegts_psi_calc_crc(psi);

    do
    {
        if(psi->type != MPEGTS_PACKET_PMT)
        {
            psi->status = MPEGTS_ERROR_PACKET_TYPE;
            break;
        }
        if(buffer[0] != 0x02) // check Table ID
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
        if(pmt->items)
        {
            psi->status = MPEGTS_CRC32_CHANGED;
            return;
        }

        psi->status = MPEGTS_ERROR_NONE;
        psi->crc32 = curr_crc;
    } while(0);

    if(psi->status != MPEGTS_ERROR_NONE)
    {
        log_error("PMT: error %d [0x%02X%02X size:%d crc:0x%08X pnr:%d]"
                  , psi->status, buffer[0], buffer[1], psi->buffer_size
                  , calc_crc, pmt->pnr);
        return;
    }

    pmt->pnr = pnr;
    pmt->pcr = ((buffer[8] & 0x1F) << 8) | buffer[9];
    pmt->version = (buffer[5] & 0x3E) >> 1;
    pmt->current_next = buffer[5] & 0x01;
    const uint16_t desc_size = ((buffer[10] & 0x0f) << 8) | buffer[11];
    buffer += 12; // skip PMT header

    if(desc_size)
    {
        pmt->desc = mpegts_desc_init(buffer, desc_size);
        buffer += desc_size;
    }

    while(buffer < buffer_end)
    {
        const uint8_t es_type = buffer[0];
        const uint16_t es_pid = ((buffer[1] & 0x1f) << 8) | buffer[2];
        const uint16_t es_desc_size = ((buffer[3] & 0x0f) << 8)
                                            | buffer[4];
        buffer += 5; // PMT item header size

        mpegts_pmt_item_t *item = calloc(1, sizeof(mpegts_pmt_item_t));
        item->pid = es_pid;
        item->type = es_type;
        if(es_desc_size > 0)
            item->desc = mpegts_desc_init(buffer, es_desc_size);
        pmt->items = list_append(pmt->items, item);
        buffer += es_desc_size;
    }
} /* mpegts_pmt_parse */

void mpegts_pmt_dump(mpegts_psi_t *psi, const char *name)
{
    mpegts_pmt_t *pmt = psi->data;
    const uint16_t pnr = pmt->pnr;

    log_info("[PMT %s] pnr:%d", name, pnr);
    log_info("[PMT %s] pid:%4d PCR", name, pmt->pcr);

    if(pmt->desc)
        mpegts_desc_dump(pmt->desc, psi->buffer[0], name);

    list_t *i = list_get_first(pmt->items);
    while(i)
    {
        mpegts_pmt_item_t *item = list_get_data(i);
        const uint16_t es_pid = item->pid;
        log_info("[PMT %s] pid:%4d %s:0x%02X", name, es_pid
                 , mpegts_pes_name(mpegts_pes_type(item->type)), item->type);
        if(item->desc)
            mpegts_desc_dump(item->desc, psi->buffer[0], name);
        i = list_get_next(i);
    }
    log_info("[PMT %s] crc:0x%08X", name, psi->crc32);
} /* mpegts_pmt_dump */

void mpegts_pmt_assemble(mpegts_psi_t *psi)
{
    mpegts_pmt_t *pmt = psi->data;

    uint8_t *buffer = psi->buffer;
    buffer[0] = 0x02; // table id
    buffer[1] = 0x80 | 0x30; // section syntax indicator | reserved
    buffer[3] = pmt->pnr >> 8;
    buffer[4] = pmt->pnr & 0xFF;
    // 0xC0 - reserved
    buffer[5] = 0xC0 | ((pmt->version << 1) & 0x3E) | (pmt->current_next & 1);
    buffer[6] = buffer[7] = 0x00; // section number and last section number

    uint8_t *ptr = &buffer[12]; // skip PMT header
    const uint16_t pcr = pmt->pcr;
    buffer[8] = 0xE0 | ((pcr >> 8) & 0x1F); // reserved | pcr
    buffer[9] = pcr & 0xFF;
    buffer[10] = 0xF0; // reserved
    buffer[11] = 0x00;
    if(pmt->desc)
    {
        const uint16_t desc_size = pmt->desc->buffer_size;
        if(desc_size > 0)
        {
            buffer[10] = 0xF0 | ((desc_size >> 8) & 0x03);
            buffer[11] = desc_size & 0xFF;
            memcpy(ptr, pmt->desc->buffer, desc_size);
            ptr += desc_size;
        }
    }


    list_t *i = list_get_first(pmt->items);
    while(i)
    {
        mpegts_pmt_item_t *item = list_get_data(i);
        ptr[0] = item->type;
        const uint16_t pid = item->pid;
        ptr[1] = (pid >> 8) & 0x1F;
        ptr[2] = pid & 0xFF;
        ptr[3] = ptr[4] = 0x00;
        if(item->desc)
        {
            const uint16_t es_desc_size = item->desc->buffer_size;
            if(es_desc_size > 0)
            {
                ptr[3] = (es_desc_size >> 8) & 0x03;
                ptr[4] = es_desc_size & 0xFF;
                memcpy(&ptr[5], item->desc->buffer, es_desc_size);
                ptr += es_desc_size;
            }
        }
        ptr += 5;
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
} /* mpegts_pmt_assemble */

void mpegts_pmt_item_add(mpegts_psi_t *psi
                         , uint16_t pid
                         , uint8_t type
                         , mpegts_desc_t *desc)
{
    mpegts_pmt_item_t *item = calloc(1, sizeof(mpegts_pmt_item_t));
    item->pid = pid;
    item->type = type;
    if(desc)
        item->desc = mpegts_desc_init(desc->buffer, desc->buffer_size);

    mpegts_pmt_t *pmt = psi->data;
    pmt->items = list_append(pmt->items, item);
}

void mpegts_pmt_item_delete(mpegts_psi_t *psi, uint16_t pid)
{
    mpegts_pmt_t *pmt = psi->data;
    list_t *i = list_get_first(pmt->items);
    while(i)
    {
        mpegts_pmt_item_t *item = list_get_data(i);
        if(item->pid == pid)
        {
            mpegts_desc_destroy(item->desc);
            free(item);
            if(i == pmt->items)
                pmt->items = list_delete(i, NULL);
            else
                list_delete(i, NULL);
            return;
        }
        i = list_get_next(i);
    }
}

mpegts_pmt_item_t * mpegts_pmt_item_get(mpegts_psi_t *psi, uint16_t pid)
{
    mpegts_pmt_t *pmt = psi->data;
    list_t *i = list_get_first(pmt->items);
    while(i)
    {
        mpegts_pmt_item_t *item = list_get_data(i);
        if(item->pid == pid)
            return item;
        i = list_get_next(i);
    }
    return NULL;
}
