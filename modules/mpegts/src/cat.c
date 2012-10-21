/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#include <astra.h>
#include "../mpegts.h"

mpegts_psi_t * mpegts_cat_init(void)
{
    mpegts_psi_t *psi = mpegts_psi_init(MPEGTS_PACKET_CAT, 1);
    mpegts_cat_t *cat = calloc(1, sizeof(mpegts_cat_t));
    psi->data = cat;
    cat->current_next = 1;
    return psi;
}

void mpegts_cat_destroy(mpegts_psi_t *psi)
{
    if(!psi)
        return;

    mpegts_cat_t *cat = psi->data;
    mpegts_desc_destroy(cat->desc);
    free(cat);
    psi->data = NULL;
    mpegts_psi_destroy(psi);
}

void mpegts_cat_parse(mpegts_psi_t *psi)
{
    mpegts_cat_t *cat = psi->data;

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
        if(psi->type != MPEGTS_PACKET_CAT)
        {
            psi->status = MPEGTS_ERROR_PACKET_TYPE;
            break;
        }
        if(buffer[0] != 0x01) // check Table ID
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
        if(cat->desc)
        {
            psi->status = MPEGTS_CRC32_CHANGED;
            return;
        }

        psi->status = MPEGTS_ERROR_NONE;
        psi->crc32 = curr_crc;
    } while(0);

    if(psi->status != MPEGTS_ERROR_NONE)
    {
        log_error("CAT: error %d [0x%02X%02X size:%d crc:0x%08X]", psi->status
                  , buffer[0], buffer[1], psi->buffer_size, calc_crc);
        return;
    }

    cat->version = (buffer[5] & 0x3E) >> 1;
    cat->current_next = buffer[5] & 0x01;

    buffer += 8; // skip CAT header

    cat->desc = mpegts_desc_init(buffer, buffer_end - buffer);
} /* mpegts_cat_parse */

void mpegts_cat_dump(mpegts_psi_t *psi, const char *name)
{
    mpegts_cat_t *cat = psi->data;
    mpegts_desc_dump(cat->desc, psi->buffer[0], name);
    log_info("[CAT %s] crc:0x%08X", name, psi->crc32);
} /* mpegts_cat_dump */
