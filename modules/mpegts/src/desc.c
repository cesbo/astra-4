/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#include <astra.h>
#include <modules/utils/utils.h>
#include "../mpegts.h"

mpegts_desc_t * mpegts_desc_init(uint8_t *buffer, size_t size)
{
    mpegts_desc_t *desc = calloc(1, sizeof(mpegts_desc_t));
    if(buffer)
    {
        memcpy(desc->buffer, buffer, size);
        desc->buffer_size = size;
        mpegts_desc_parse(desc);
    }
    return desc;
}

void mpegts_desc_destroy(mpegts_desc_t *desc)
{
    if(!desc)
        return;

    list_t *i = desc->items;
    while(i)
        i = list_delete(i, NULL);
    free(desc);
}

void mpegts_desc_parse(mpegts_desc_t *desc)
{
    uint8_t *buffer = desc->buffer;
    uint8_t * const buffer_end = buffer + desc->buffer_size;
    while(buffer < buffer_end)
    {
        desc->items = list_append(desc->items, buffer);
        buffer += (buffer[1] + 2); // 2 - desc + size
    }
}

void mpegts_desc_dump(mpegts_desc_t *desc, uint8_t table_id, const char *name)
{
    char str[0xFF * 2]; // max of single desc length

    const char *tname;
    switch(table_id)
    {
        case 0x01:
            tname = "CAT";
            break;
        case 0x02:
            tname = "PMT";
            break;
        case 0x42:
        case 0x46:
            tname = "SDT";
            break;
        default:
            tname = "";
            break;
    }

#define LOG_MSG(_msg) "[%s %s] > " _msg, tname, name

    list_t *i = list_get_first(desc->items);
    while(i)
    {
        uint8_t *b = list_get_data(i);
        switch(b[0])
        {
            case 0x09:
            {
                const uint16_t ca_pid = ((b[4] & 0x1F) << 8) | b[5];
                const uint8_t ca_info_size = b[1] - 4; // 4 - caid + capid
                if(ca_info_size > 0)
                {
                    const char str_data[] = " data:";
                    memcpy(str, str_data, sizeof(str_data) - 1);
                    hex_to_str(&str[sizeof(str_data) - 1], &b[6]
                               , ca_info_size);
                }
                else
                    str[0] = '\0';
                log_info(LOG_MSG("cas: caid:0x%02X%02X pid:%d%s")
                         , b[2], b[3], ca_pid, str);
                break;
            }
            case 0x0A:
            {
                log_info(LOG_MSG("language: %c%c%c"), b[2], b[3], b[4]);
                break;
            }
// TODO: extended descriptor info
            default:
            {
                hex_to_str(str, &b[2], b[1]);
                log_info(LOG_MSG("descriptor:0x%02X size:%d data:0x%s")
                         , b[0], b[1], str);
                break;
            }
        }
        i = list_get_next(i);
    }
}

void mpegts_desc_assemble(mpegts_desc_t *desc)
{
    size_t tmp_skip = 0;
    uint8_t tmp[DESC_MAX_SIZE];

    list_t *i = list_get_first(desc->items);
    while(i)
    {
        uint8_t *b = list_get_data(i);
        const uint8_t size = b[1] + 2; // 2 - desc + size
        memcpy(&tmp[tmp_skip], b, size);
        tmp_skip += size;
        i = list_get_next(i);
    }

    memcpy(desc->buffer, tmp, tmp_skip);
    desc->buffer_size = tmp_skip;
}
