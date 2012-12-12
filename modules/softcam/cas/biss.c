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
};

/* cas api */

static int biss_check_caid(uint16_t caid)
{
    return (caid == 0x2600);
}

static cam_packet_t * biss_check_em(cas_data_t *cas, const uint8_t *payload)
{
    if(cas->is_keys)
        return 0;
    cas->is_keys = 1;
    return cam_packet_init(cas, payload, MPEGTS_PACKET_ECM);
}

static int biss_check_keys(cam_packet_t *packet)
{
    return 1;
}

static uint16_t biss_check_desc(cas_data_t *cas, const uint8_t *desc)
{
    return 0;
}

CAS_MODULE(biss);
