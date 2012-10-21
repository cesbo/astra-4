/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#include <astra.h>
#include "../mpegts.h"

mpegts_psi_t * mpegts_sdt_init(void)
{
    mpegts_psi_t *psi = mpegts_psi_init(MPEGTS_PACKET_SDT, 17);
    mpegts_sdt_t *sdt = calloc(1, sizeof(mpegts_sdt_t));
    psi->data = sdt;
    sdt->current_next = 1;
    return psi;
}

void mpegts_sdt_destroy(mpegts_psi_t *psi)
{
    if(!psi)
        return;

    mpegts_sdt_t *sdt = psi->data;

    list_t *i = sdt->items;
    while(i)
    {
        mpegts_sdt_item_t *item = list_get_data(i);
        mpegts_desc_destroy(item->desc);
        free(item);
        i = list_delete(i, NULL);
    }

    free(sdt);
    psi->data = NULL;
    mpegts_psi_destroy(psi);
}

void mpegts_sdt_parse(mpegts_psi_t *psi)
{
    mpegts_sdt_t *sdt = psi->data;

    const uint32_t curr_crc = mpegts_psi_get_crc(psi);
    if(sdt->items && psi->crc32 == curr_crc)
    {
        psi->status = MPEGTS_UNCHANGED;
        return;
    }
    const uint32_t calc_crc = mpegts_psi_calc_crc(psi);

    uint8_t *buffer = psi->buffer;
    uint8_t * const buffer_end = buffer + psi->buffer_size - CRC32_SIZE;

    do
    {
        if(psi->type != MPEGTS_PACKET_SDT)
        {
            psi->status = MPEGTS_ERROR_PACKET_TYPE;
            break;
        }
        if(buffer[0] != 0x42 && buffer[0] != 0x46) // check Table ID
        {
            psi->status = MPEGTS_ERROR_TABLE_ID;
            break;
        }
        if((buffer[1] & 0x8C) != 0x80) // check fixed bits
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
        if(sdt->items)
        {
            psi->status = MPEGTS_CRC32_CHANGED;
            return;
        }

        psi->status = MPEGTS_ERROR_NONE;
        psi->crc32 = curr_crc;
    } while(0);

    if(psi->status != MPEGTS_ERROR_NONE)
    {
        log_error("SDT: error %d [0x%02X%02X size:%d crc:0x%08X]", psi->status
                  , buffer[0], buffer[1], psi->buffer_size, calc_crc);
        return;
    }

    sdt->stream_id = (buffer[3] << 8) | buffer[4];
    sdt->version = (buffer[5] & 0x3E) >> 1;
    sdt->current_next = buffer[5] & 0x01;
    sdt->network_id = (buffer[8] << 8) | buffer[9];

    buffer += 11; // skip SDT header

    while(buffer < buffer_end)
    {
        const uint16_t item_pnr = (buffer[0] << 8) | buffer[1];
        const uint16_t item_desc_size = ((buffer[3] & 0x0f) << 8) | buffer[4];
        buffer += 5; // SDT item header size

        mpegts_sdt_item_t *item = calloc(1, sizeof(mpegts_sdt_item_t));
        item->pnr = item_pnr;
        if(item_desc_size > 0)
            item->desc = mpegts_desc_init(buffer, item_desc_size);
        sdt->items = list_append(sdt->items, item);

        buffer += item_desc_size;
    }
} /* mpegts_sdt_parse */

void mpegts_sdt_dump(mpegts_psi_t *psi, const char *name)
{
    mpegts_sdt_t *sdt = psi->data;

    log_info("[SDT %s] stream_id:%d", name, sdt->stream_id);
    log_info("[SDT %s] network_id:%d", name, sdt->network_id);

    list_t *i = list_get_first(sdt->items);
    while(i)
    {
        mpegts_sdt_item_t *item = list_get_data(i);
        log_info("[SDT %s] pnr:%d", name, item->pnr);
        if(item->desc)
            mpegts_desc_dump(item->desc, psi->buffer[0], name);
        i = list_get_next(i);
    }

    log_info("[SDT %s] crc:0x%08X", name, psi->crc32);
} /* mpegts_sdt_dump */
