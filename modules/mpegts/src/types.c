/*
 * Astra MPEG-TS Module: types
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#include <stdio.h>
#include "../mpegts.h"

const char * mpegts_type_name(mpegts_packet_type_t type)
{
    switch(type)
    {
        case MPEGTS_PACKET_PAT:
            return "PAT";
        case MPEGTS_PACKET_CAT:
            return "CAT";
        case MPEGTS_PACKET_PMT:
            return "PMT";
        case MPEGTS_PACKET_VIDEO:
            return "VIDEO";
        case MPEGTS_PACKET_AUDIO:
            return "AUDIO";
        case MPEGTS_PACKET_SUB:
            return "SUB";
        case MPEGTS_PACKET_DATA:
            return "DATA";
        case MPEGTS_PACKET_ECM:
            return "ECM";
        case MPEGTS_PACKET_EMM:
            return "EMM";
        default:
            return "UNKN";
    }
}

mpegts_packet_type_t mpegts_pes_type(uint8_t type_id)
{
    switch(type_id)
    {
        case 0x01:  // MPEG-1 video
        case 0x02:  // MPEG-2 video
        case 0x80:  // MPEG-2 MOTO video
        case 0x10:  // MPEG-4 video
        case 0x1B:  // H264 video
        case 0xA0:  // MSCodec vlc video
            return MPEGTS_PACKET_VIDEO;
        case 0x03:  // MPEG-1 audio
        case 0x04:  // MPEG-2 audio
        case 0x06:  // AC3 audio
        case 0x11:  // MPEG-4 audio (LATM)
        case 0x0F:  // Audio with ADTS
        case 0x81:  // A52 audio
        case 0x83:  // LPCM audio
        case 0x84:  // SDDS audio
        case 0x85:  // DTS audio
        case 0x87:  // E-AC3
        case 0x91:  // A52 vls audio
        case 0x94:  // SDDS audio
            return MPEGTS_PACKET_AUDIO;
        case 0x82:  // DVB_SPU
        case 0x92:  // DVB_SPU vls
            return MPEGTS_PACKET_SUB;
        case 0x12:  // MPEG-4 generic (sub/scene/...)
        case 0xEA:  // privately managed ES
        default:
            return MPEGTS_PACKET_DATA;
    }
}

/* ISO/IEC 14496-2 */
const char * mpeg4_profile_level_name(uint8_t type_id)
{
    switch(type_id)
    {
        case 0x01: return "Simple/L1";
        case 0x02: return "Simple/L2";
        case 0x03: return "Simple/L3";
        case 0x11: return "Simple Scalable/L1";
        case 0x12: return "Simple Scalable/L2";
        case 0x21: return "Core/L1";
        case 0x22: return "Core/L2";
        case 0x32: return "Main/L2";
        case 0x33: return "Main/L3";
        case 0x34: return "Main/L4";
        case 0x42: return "N-bit/L2";
        case 0x51: return "Scalable Texture/L1";
        case 0x61: return "Simple Face Animation/L1";
        case 0x62: return "Simple Face Animation/L2";
        case 0x63: return "Simple FBA/L1";
        case 0x64: return "Simple FBA/L2";
        case 0x71: return "Basic Animated Texture/L1";
        case 0x72: return "Basic Animated Texture/L2";
        case 0x81: return "Hybrid/L1";
        case 0x82: return "Hybrid/L2";
        case 0x91: return "Advanced Real Time Simple/L1";
        case 0x92: return "Advanced Real Time Simple/L2";
        case 0x93: return "Advanced Real Time Simple/L3";
        case 0x94: return "Advanced Real Time Simple/L4";
        default:
            return "Unknown Profile/Level";
    }
}

static void push_iso8859_1_text(luaL_Buffer *b, const uint8_t *data, uint8_t size)
{
    for(uint8_t i = 0; i < size; ++i)
    {
        uint8_t c = data[i];
        if(c < 0x80)
        {
            luaL_addchar(b, c);
        }
        else
        {
            luaL_addchar(b, 0xC0 | ((c & 0xC0) >> 6));
            luaL_addchar(b, 0x80 | (c & 0x3F));
        }
    }
}

static void push_iso8859_5_text(luaL_Buffer *b, const uint8_t *data, uint8_t size)
{
    uint8_t c, u1, u2;

    for(uint8_t i = 0; i < size; ++i)
    {
        c = data[i];

        if(c < 0x80)
        {
            luaL_addchar(b, c);
            continue;
        }

        u1 = 0xD0;
        u2 = 0x80 | (c & 0x1F);

        if(c >= 0xE0) u1 |= 0x01;
        else if(c >= 0xC0) u2 |= 0x20;

        luaL_addchar(b, u1);
        luaL_addchar(b, u2);
    }
}

static void push_iso8859_7_text(luaL_Buffer *b, const uint8_t *data, uint8_t size)
{
    uint8_t c, u1, u2;

    for(uint8_t i = 0; i < size; ++i)
    {
        c = data[i];

        if(c < 0x80)
        {
            luaL_addchar(b, c);
            continue;
        }

        u1 = 0xCE;
        u2 = c - 0x30;

        if(c >= 0xF0)
        {
            u1 |= 0x01;
            u2 -= 0x40;
        }

        luaL_addchar(b, u1);
        luaL_addchar(b, u2);
    }
}

static void push_description_text(const uint8_t *data)
{
    char s[3];

    luaL_Buffer b;
    luaL_buffinit(lua, &b);

    const uint8_t size = *data++;
    const uint8_t charset_id = *data;

    switch(charset_id)
    {
        case 0x01: push_iso8859_5_text(&b, &data[1], size - 1); break; // Cyrillic
        case 0x03: push_iso8859_7_text(&b, &data[1], size - 1); break; // Greek
        default:
        {
            if(charset_id >= 0x20)
                push_iso8859_1_text(&b, data, size);
            else
            {
                luaL_addstring(&b, "unknown charset: 0x");
                for(uint8_t i = 0; i < size; ++i)
                {
                    snprintf(s, sizeof(s), "%02X", data[i]);
                    luaL_addstring(&b, s);
                }
            }
        }
    }

    luaL_pushresult(&b);
}

#define HEX_PREFIX_SIZE 2
#define LINE_END_SIZE 1

static const char __data[] = "data";
static const char __type_name[] = "type_name";
static const char __strip[] = "... (strip)";

void mpegts_desc_to_lua(const uint8_t *desc)
{
    char data[128];

    lua_newtable(lua);

    lua_pushnumber(lua, desc[0]);
    lua_setfield(lua, -2, "type_id");

    switch(desc[0])
    {
        case 0x09:
        { /* CA */
            lua_pushstring(lua, "cas");
            lua_setfield(lua, -2, __type_name);

            const uint16_t ca_pid = DESC_CA_PID(desc);
            const uint16_t caid = desc[2] << 8 | desc[3];

            lua_pushnumber(lua, caid);
            lua_setfield(lua, -2, "caid");

            lua_pushnumber(lua, ca_pid);
            lua_setfield(lua, -2, "pid");

            const uint8_t ca_info_size = desc[1] - 4; // 4 = caid + ca_pid
            if(ca_info_size > 0)
            {
                const int max_size = ((HEX_PREFIX_SIZE
                                      + ca_info_size * 2
                                      + LINE_END_SIZE) > (int)sizeof(data))
                                   ? ((int)sizeof(data)
                                      - HEX_PREFIX_SIZE
                                      - (int)sizeof(__strip)
                                      - LINE_END_SIZE) / 2
                                   : ca_info_size;

                data[0] = '0';
                data[1] = 'x';
                hex_to_str(&data[HEX_PREFIX_SIZE], &desc[6], max_size);
                if(max_size != ca_info_size)
                    sprintf(&data[HEX_PREFIX_SIZE + max_size], "%s", __strip);

                lua_pushstring(lua, data);
                lua_setfield(lua, -2, __data);
            }
            break;
        }
        case 0x0A:
        { /* ISO-639 language */
            static const char __lang[] = "lang";
            lua_pushstring(lua, __lang);
            lua_setfield(lua, -2, __type_name);

            sprintf(data, "%c%c%c", desc[2], desc[3], desc[4]);
            lua_pushstring(lua, data);
            lua_setfield(lua, -2, __lang);
            break;
        }
        case 0x48:
        { /* Service Descriptor */
            lua_pushstring(lua, "service");
            lua_setfield(lua, -2, __type_name);

            lua_pushnumber(lua, desc[2]);
            lua_setfield(lua, -2, "service_type_id");

            desc += 3;
            // service provider
            push_description_text(desc);
            lua_setfield(lua, -2, "service_provider");

            desc += desc[0] + 1;
            // service name
            push_description_text(desc);
            lua_setfield(lua, -2, "service_name");

            break;
        }
        case 0x52:
        { /* Stream Identifier */
            static const char __stream_id[] = "stream_id";
            lua_pushstring(lua, __stream_id);
            lua_setfield(lua, -2, __type_name);

            lua_pushnumber(lua, desc[2]);
            lua_setfield(lua, -2, __stream_id);
            break;
        }
        default:
        {
            lua_pushstring(lua, "unknown");
            lua_setfield(lua, -2, __type_name);

            const int desc_size = 2 + desc[1];
            const int max_size = ((HEX_PREFIX_SIZE
                                  + desc_size * 2
                                  + LINE_END_SIZE) > (int)sizeof(data))
                               ? ((int)sizeof(data)
                                  - HEX_PREFIX_SIZE
                                  - (int)sizeof(__strip)
                                  - LINE_END_SIZE) / 2
                               : desc_size;

            data[0] = '0';
            data[1] = 'x';
            hex_to_str(&data[HEX_PREFIX_SIZE], desc, max_size);
            if(max_size != desc_size)
                sprintf(&data[HEX_PREFIX_SIZE + max_size], "%s", __strip);

            lua_pushstring(lua, data);
            lua_setfield(lua, -2, __data);

            break;
        }
    }
}
