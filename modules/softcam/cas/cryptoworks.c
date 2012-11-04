/*
 * Astra SoftCAM module
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 *               2011, Santa77 <santa77@fibercom.sk>
 *
 * This module is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This module is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this module.  If not, see <http://www.gnu.org/licenses/>.
 *
 * For more information, visit http://cesbo.com
 */

#include "../softcam.h"

/* to access to the cam module information */
struct module_data_s
{
    CAM_MODULE_BASE();
};

struct cas_data_s
{
    CAS_MODULE_BASE();

    uint8_t parity;
};

/* cas api */

static int cryptoworks_check_caid(uint16_t caid)
{
    return ((caid & 0xFF00) == 0x0D00);
}

/* check Entitlement Message */
static cam_packet_t * cryptoworks_check_em(cas_data_t *cas
                                           , const uint8_t *payload)
{
    const uint8_t em_type = payload[0];
    switch(em_type)
    {
        // ECM
        case 0x80:
        case 0x81:
        {
            if(em_type != cas->parity)
            {
                cas->parity = em_type;
                return cam_packet_init(cas, payload, MPEGTS_PACKET_ECM);
            }
            break;
        }
        case 0x82:
        { // unique
            if(payload[3] == 0xA9 && payload[4] == 0xFF
               && payload[13] == 0x80 && payload[14] == 0x05
               && !memcmp(&payload[5], &CAS2CAM(cas).ua[3], 5))
            {
                return cam_packet_init(cas, payload, MPEGTS_PACKET_EMM);
            }
            break;
        }
        case 0x84:
        { // shared
            if(payload[3] == 0xA9 && payload[4] == 0xFF
               && payload[12] == 0x80 && payload[13] == 0x04
               && !memcmp(&payload[5], &CAS2CAM(cas).ua[3], 4))
            {
                return cam_packet_init(cas, payload, MPEGTS_PACKET_EMM);
            }
            break;
        }
        case 0x86:
        { // global
            if(payload[3] == 0xA9 && payload[4] == 0xFF
               && payload[5] == 0x83 && payload[6] == 0x01
               && payload[8] == 0x85) /* payload[8] == 0x84 is not known */
            {
                return cam_packet_init(cas, payload, MPEGTS_PACKET_EMM);
            }
            break;
        }
        case 0x88:
        case 0x89:
        { // global
            if(payload[3] == 0xA9 && payload[4] == 0xFF
               && payload[8] == 0x83 && payload[9] == 0x01)
            {
                return cam_packet_init(cas, payload, MPEGTS_PACKET_EMM);
            }
            break;
        }
        default:
            break;
    }

    return NULL;
}

static int cryptoworks_check_keys(cam_packet_t *packet)
{
    return 1; // if 0, then cas don't send any message to decrypt module
}

/*
 * CA descriptor (iso13818-1):
 * tag      :8 (must be 0x09)
 * length   :8
 * caid     :16
 * reserved :3
 * pid      :13
 * data     :length-4
 */

static uint16_t cryptoworks_check_desc(cas_data_t *cas, const uint8_t *desc)
{
    return CA_DESC_PID(desc);
}

CAS_MODULE(cryptoworks);
