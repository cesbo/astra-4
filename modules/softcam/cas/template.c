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

#include "../module_cam.h"

struct cas_data_t
{
    int empty;
};

static bool check_em(module_cas_t *cas, mpegts_psi_t *em)
{
    return false;
}

static bool check_key(module_cas_t *cas, uint8_t parity, const uint8_t *key)
{
    return false;
}

static bool check_descriptor(module_cas_t *cas, const uint8_t *desc)
{
    return false;
}

static module_cas_t * cas_init(uint16_t caid, const uint8_t *cas_data)
{
    if(caid != 0xFFFF)
        return NULL;

    module_cas_t *cas = malloc(sizeof(module_cas_t));
    cas->check_em = check_em;
    cas->check_key = check_key;
    cas->data = calloc(1, sizeof(cas_data_t));

    return cas;
}

const module_cas_t template =
{
    .init = cas_init,
    .check_descriptor = check_descriptor,
    .check_em = check_em,
    .check_key = check_key
};
