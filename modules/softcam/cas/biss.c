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

struct module_data_s
{
    CAM_MODULE_BASE();
};

struct cas_data_s
{
    CAS_MODULE_BASE();

    int is_keys;
    uint8_t keys[32];
};

/* cas api */

static int biss_check_caid(uint16_t caid)
{
    return (caid == 0x2600);
}

static cam_packet_t * biss_check_em(cas_data_t *cas, const uint8_t *payload)
{
    return cam_packet_init(cas, payload, MPEGTS_PACKET_ECM);
}

static int biss_check_keys(cam_packet_t *packet)
{
    return 1;
}

static uint16_t biss_check_desc(cas_data_t *cas, const uint8_t *desc)
{
    if(!cas->is_keys)
    {
        cas->keys[0] = 0x80;
        cas->keys[1] = 0x10;
        cas->keys[2] = 0x10;
        memcpy(&cas->keys[3], CAS2CAM(cas).cas_data, 8);
        if(cas->keys[6] == 0x00)
            cas->keys[6] = (cas->keys[3] + cas->keys[4] + cas->keys[5]) & 0xFF;
        if(cas->keys[10] == 0x00)
            cas->keys[10] = (cas->keys[7] + cas->keys[8] + cas->keys[9]) & 0xFF;
        memcpy(&cas->keys[11], &cas->keys[3], 8);

        cam_send(cas->__cas_module.cam, cas, cas->keys);
        cas->is_keys = 1;
    }

    return 0xFFFF;
}

CAS_MODULE(biss);
