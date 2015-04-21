/*
 * Astra Module: MPEG-TS (extended functions)
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
        case 0x01:  // ISO/IEC 11172 Video
        case 0x02:  // ISO/IEC 13818-2 Video
        case 0x10:  // ISO/IEC 14496-2 Visual
        case 0x1B:  // ISO/IEC 14496-10 Video | H.264
        case 0x24:  // ISO/IEC 23008-2 Video | H.265
            return MPEGTS_PACKET_VIDEO;
        case 0x03:  // ISO/IEC 11172 Audio
        case 0x04:  // ISO/IEC 13818-3 Audio
        case 0x0F:  // ISO/IEC 13818-7 Audio (ADTS)
        case 0x11:  // ISO/IEC 14496-3 Audio (LATM)
            return MPEGTS_PACKET_AUDIO;
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

static void push_description_text(const uint8_t *data)
{
    luaL_Buffer b;
    luaL_buffinit(lua, &b);

    char *text = iso8859_decode(&data[1], data[0]);
    luaL_addstring(&b, text);
    free(text);

    luaL_pushresult(&b);
}

#define HEX_PREFIX_SIZE 2
#define LINE_END_SIZE 1

static const char __data[] = "data";
static const char __type_name[] = "type_name";
static const char __strip[] = "... (strip)";

__asc_inline
char asc_safe_char(char c)
{
    return (c > 0x1f && c < 0x7f) ? c : '.';
}

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

            char lang[4];
            lang[0] = asc_safe_char((char)desc[2]);
            lang[1] = asc_safe_char((char)desc[3]);
            lang[2] = asc_safe_char((char)desc[4]);
            lang[3] = 0x00;

            lua_pushstring(lua, lang);
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
            if(desc[0] > 0)
                push_description_text(desc);
            else
                lua_pushstring(lua, "");
            lua_setfield(lua, -2, "service_provider");

            desc += desc[0] + 1;
            // service name
            if(desc[0] > 0)
                push_description_text(desc);
            else
                lua_pushstring(lua, "");
            lua_setfield(lua, -2, "service_name");

            break;
        }
        case 0x4D:
        { /* Short Even */
            lua_pushstring(lua, "short_event_descriptor");
            lua_setfield(lua, -2, __type_name);

            const char lang[] = { desc[2], desc[3], desc[4], 0x00 };
            lua_pushstring(lua, lang);
            lua_setfield(lua, -2, "lang");

            desc += 5; // skip 1:tag + 1:length + 3:lang
            push_description_text(desc);
            lua_setfield(lua, -2, "event_name");

            desc += desc[0] + 1;
            push_description_text(desc);
            lua_setfield(lua, -2, "text_char");

            break;
        }
        case 0x4E:
        { /* Extended Event */
            lua_pushstring(lua, "extended_event_descriptor");
            lua_setfield(lua, -2, __type_name);

            lua_pushnumber(lua, desc[2] >> 4);
            lua_setfield(lua, -2, "desc_num");

            lua_pushnumber(lua, desc[2] & 0x0F);
            lua_setfield(lua, -2, "last_desc_num");

            const char lang[] = { desc[3], desc[4], desc[5], 0x00 };
            lua_pushstring(lua, lang);
            lua_setfield(lua, -2, "lang");

            desc += 6; // skip 1:tag + 1:length + 1:desc_num:last_desc_num + 3:lang

            if(desc[0] > 0)
            {
                lua_newtable(lua); // items[]

                const uint8_t *_item_ptr = &desc[1];
                const uint8_t *const _item_ptr_end = _item_ptr + desc[0];

                while(_item_ptr < _item_ptr_end)
                {
                    const int item_count = luaL_len(lua, -1) + 1;
                    lua_pushnumber(lua, item_count);

                    lua_newtable(lua);

                    push_description_text(_item_ptr);
                    lua_setfield(lua, -2, "item_desc");
                    _item_ptr += _item_ptr[0] + 1;

                    push_description_text(_item_ptr);
                    lua_setfield(lua, -2, "item");
                    _item_ptr += _item_ptr[0] + 1;

                    lua_settable(lua, -3);
               }

               lua_setfield(lua, -2, "items");
            }

            desc += desc[0] + 1;
            // text
            if(desc[0] > 0)
                 push_description_text(desc);
            else
                 lua_pushstring(lua, "");
            lua_setfield(lua, -2, "text");

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
        case 0x54:
        { /* Content [category] */
            lua_pushstring(lua, "content_descriptor");
            lua_setfield(lua, -2, __type_name);

            lua_newtable(lua); // items[]

            const uint8_t *_item_ptr = &desc[2];
            const uint8_t *const _item_ptr_end = _item_ptr + desc[1];

            while(_item_ptr < _item_ptr_end)
            {
                const int item_count = luaL_len(lua, -1) + 1;
                lua_pushnumber(lua, item_count);

                lua_newtable(lua);

                lua_pushnumber(lua, _item_ptr[0] >> 4);
                lua_setfield(lua, -2, "cn_l1");
                lua_pushnumber(lua, _item_ptr[0] & 0xF);
                lua_setfield(lua, -2, "cn_l2");
                lua_pushnumber(lua, _item_ptr[1] >> 4);
                lua_setfield(lua, -2, "un_l1");
                lua_pushnumber(lua, _item_ptr[1] & 0xF);
                lua_setfield(lua, -2, "un_l2");

                lua_settable(lua, -3);
                _item_ptr += 2;
            }
            lua_setfield(lua, -2, "items");

            break;
        }
        case 0x55: // Parental Rating Descriptor [rating]
        {
            lua_pushstring(lua, "parental_rating_descriptor");
            lua_setfield(lua, -2, __type_name);

            lua_newtable(lua); // items[]

            const uint8_t *_item_ptr = &desc[2];
            const uint8_t *const _item_ptr_end = _item_ptr + desc[1];

            while(_item_ptr < _item_ptr_end)
            {
                const int item_count = luaL_len(lua, -1) + 1;
                lua_pushnumber(lua, item_count);

                const char country[] = { _item_ptr[0], _item_ptr[1], _item_ptr[2], 0x00 };
                lua_pushstring(lua, country);
                lua_setfield(lua, -2, "country");

                lua_pushnumber(lua, _item_ptr[3]);
                lua_setfield(lua, -2, "rating");

                lua_settable(lua, -3);
                _item_ptr += 4;
            }
            lua_setfield(lua, -2, "items");

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
