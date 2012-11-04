/*
 * Astra SoftCAM module
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
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

    uint8_t *sa;
};

/* cas api */

static int mediaguard_check_caid(uint16_t caid)
{
    return (caid == 0x0100);
}

/* check Entitlement Message */
static cam_packet_t * mediaguard_check_em(cas_data_t *cas
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
        // EMM ( ret = MPEGTS_PACKET_EMM )
        case 0x82:
        {
            if(!memcmp(&payload[3], &CAS2CAM(cas).ua[2], 6))
                return cam_packet_init(cas, payload, MPEGTS_PACKET_EMM);
            break;
        }
        case 0x84:
        {
            if(!memcmp(&payload[5], &cas->sa[5], 3))
                return cam_packet_init(cas, payload, MPEGTS_PACKET_EMM);
            break;
        }
        default:
            break;
    }

    return NULL;
}

/* keys = 3bytes header + 2x64bit control words */
static int mediaguard_check_keys(cam_packet_t *packet)
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

static uint16_t mediaguard_check_desc(cas_data_t *cas, const uint8_t *desc)
{
    const int length = desc[1] - 4;
    if(length < 2)
        return 0;

    list_t *i = list_get_first(CAS2CAM(cas).prov_list);
    while(i)
    {
        uint8_t *prov = list_get_data(i);
        if((prov[1] == desc[6]) && (prov[2] == desc[7]))
        {
            if(!cas->sa)
                cas->sa = &prov[3];
            return CA_DESC_PID(desc);
        }
        i = list_get_next(i);
    }

    return 0;
}

CAS_MODULE(mediaguard);
