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
            return "UNKNOWN";
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
        case 0x06:  // PES_PRIVATE
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

void mpegts_desc_to_string(char *str, uint32_t len, const uint8_t *desc)
{
    uint32_t skip = 0;
    switch(desc[0])
    {
        case 0x09:
        { /* CA */
            const uint16_t ca_pid = DESC_CA_PID(desc);
            skip = snprintf(str, len, "CAS: caid:0x%02X%02X pid:%d", desc[2], desc[3], ca_pid);
            const uint8_t ca_info_size = desc[1] - 4; // 4 = caid + ca_pid
            if(ca_info_size > 0)
            {
                skip += snprintf(&str[skip], len - skip, " data:");
                if((uint32_t)(ca_info_size * 2 + 1) > (len - skip))
                    snprintf(&str[skip], len - skip, "ERR (is too long)");
                else
                    hex_to_str(&str[skip], &desc[6], ca_info_size);
            }
            break;
        }
        case 0x0A:
        { /* ISO-639 language */
            snprintf(str, len, "Language: %c%c%c", desc[2], desc[3], desc[4]);
            break;
        }
        case 0x0E:
        { /* Maximum bitrate */
            const uint32_t max_bitrate = ((desc[2] & 0x3F) << 16) | (desc[3] << 8) | desc[4];
            snprintf(str, len, "Maximum Bitrate: %uKbit/s", (max_bitrate * 50 * 8) / 1000);
            break;
        }
        case 0x1B:
        { /* MPEG-4 video */
            snprintf(str, len, "MPEG-4 Video Profile/Level: %s"
                     , mpeg4_profile_level_name(desc[2]));
            break;
        }
        case 0x52:
        { /* Stream Identifier */
            snprintf(str, len, "Stream ID: %d", desc[2]);
            break;
        }
        default:
        {
            skip = snprintf(str, len, "descriptor:0x%02X size:%d data:0x", desc[0], desc[1]);
            if((uint32_t)(desc[1] * 2 + 1) > (len - skip))
                snprintf(&str[skip], len - skip, "ERR (is too long)");
            else
                hex_to_str(&str[skip], &desc[2], desc[1]);
            break;
        }
    }
}
