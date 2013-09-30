/*
 * Astra Module: ISO-8859
 * http://cesbo.com/astra
 *
 * Copyright (C) 2013, Andrey Dyldin <and@cesbo.com>
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

#include <astra.h>

static uint8_t * iso8859_1_text(const uint8_t *data, size_t size)
{
    uint8_t *text = malloc(size * 2 + 1);
    size_t i = 0, j = 0;

    while(i < size)
    {
        uint8_t c = data[i];
        if(c < 0x80)
        {
            if(!c) break;
            text[j++] = c;
        }
        else
        {
            text[j++] = 0xC0 | ((c & 0xC0) >> 6);
            text[j++] = 0x80 | (c & 0x3F);
        }
        ++i;
    }

    text[j] = '\0';
    return text;
}

static uint8_t * iso8859_5_text(const uint8_t *data, uint8_t size)
{
    uint8_t *text = malloc(size * 2 + 1);
    uint8_t c, u1, u2;
    size_t i = 0, j = 0;

    while(i < size)
    {
        c = data[i];

        if(c < 0x80)
        {
            if(!c) break;
            text[j++] = c;
        }
        else
        {
            u1 = 0xD0;
            u2 = 0x80 | (c & 0x1F);

            if(c >= 0xE0) u1 |= 0x01;
            else if(c >= 0xC0) u2 |= 0x20;

            text[j++] = u1;
            text[j++] = u2;
        }
        ++i;
    }

    text[j] = '\0';
    return text;
}

static uint8_t * iso8859_7_text(const uint8_t *data, uint8_t size)
{
    uint8_t *text = malloc(size * 2 + 1);
    uint8_t c, u1, u2;
    size_t i = 0, j = 0;

    while(i < size)
    {
        c = data[i];

        if(c < 0x80)
        {
            if(!c) break;
            text[j++] = c;
        }
        else
        {
            u1 = 0xCE;
            u2 = c - 0x30;

            if(c >= 0xF0)
            {
                u1 |= 0x01;
                u2 -= 0x40;
            }

            text[j++] = u1;
            text[j++] = u2;
        }
    }

    text[j] = '\0';
    return text;
}

char * iso8859_text(const uint8_t *data)
{
    const uint8_t size = *data++;
    const uint8_t charset_id = *data;

    switch(charset_id)
    {
        case 0x00: return strdup("not set");
        case 0x01: return (char *)iso8859_5_text(&data[1], size - 1); // Cyrillic
        case 0x03: return (char *)iso8859_7_text(&data[1], size - 1); // Greek
        default:
        {
            if(charset_id >= 0x20)
                return (char *)iso8859_1_text(data, size);
            else
            {
                static const char unknown_charset[] = "unknown charset: 0x";
                size_t skip = sizeof(unknown_charset) - 1;
                char *text = malloc(skip + (size * 2) + 1);
                memcpy(text, unknown_charset, sizeof(unknown_charset));
                for(uint8_t i = 0; i < size; ++i)
                    skip += sprintf(&text[skip], "%02X", data[i]);
                return text;
            }
        }
    }

    return NULL;
}
