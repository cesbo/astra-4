/*
 * Astra SoftCAM module
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 *               2012, Georgi Chorbadzhiyski <gf@unixsol.org>
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

static int bulcrypt_check_caid(uint16_t caid)
{
    return (caid == 0x4AEE || caid == 0x5581);
}

/* check Entitlement Message */
static const uint8_t * bulcrypt_check_em(cas_data_t *cas
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
                return payload;
            }
            break;
        }
        case 0x82:
        case 0x85:
        { // unique
            if(!memcmp(&payload[3], &CAS2CAM(cas).ua[2], 3))
                return payload;
            break;
        }
        case 0x84:
        { // shared
            if(!memcmp(&payload[3], &CAS2CAM(cas).ua[2], 2))
                return payload;
            break;
        }
        case 0x8b:
        { //shared-unknown
            if(!memcmp(&payload[4], &CAS2CAM(cas).ua[2], 2))
                return payload;
            break;
        }
        case 0x8a:
        { // global
            if(payload[4] == CAS2CAM(cas).ua[2])
                return payload;
            break;
        }
        default:
            break;
    }

    return NULL;
}

static int bulcrypt_check_keys(cas_data_t *cas, const uint8_t *keys)
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

static uint16_t bulcrypt_check_desc(cas_data_t *cas, const uint8_t *desc)
{
    return CA_DESC_PID(desc);
}

CAS_MODULE(bulcrypt);
