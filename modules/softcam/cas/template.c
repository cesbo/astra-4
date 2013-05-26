/*
 * Astra SoftCAM Module
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
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
 */

#include <astra.h>
#include "../module_cam.h"

struct cas_t
{
    CAS_DATA();
};

static int template_cas_check_em(cas_t *cas, em_packet_t *packet)
{
    return 1;
}

static int template_cas_check_keys(cas_t *cas, em_packet_t *packet)
{
    return 1;
}

static void template_cas_destroy(cas_t *cas)
{
    free(cas);
}

cas_t * template_cas_init(uint16_t caid, uint16_t pnr)
{
    if(caid != 0xFFFF)
        return NULL;

    cas_t *cas = calloc(1, sizeof(cas_t));
    cas_data_set(cas
                 , template_cas_check_em
                 , template_cas_check_keys
                 , template_cas_destroy);

    return cas;
}
