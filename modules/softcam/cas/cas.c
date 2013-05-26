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

extern cas_t * template_cas_init(uint16_t caid, uint16_t pnr);

typedef cas_t *(*cas_init_t)(uint16_t caid, uint16_t pnr);
static const cas_init_t cas_init_list[] =
{
    template_cas_init,
    NULL
};

cas_t * cas_init(uint16_t caid, uint16_t pnr)
{
    for(int i = 0; cas_init_list[i]; ++i)
    {
        cas_t *cas = cas_init_list[i](caid, pnr);
        if(cas)
            return cas;
    }
    return NULL;
}

struct cas_t
{
    CAS_DATA();
};

inline void cas_destroy(cas_t *cas)
{
    cas->__cas.destroy(cas);
}

inline int cas_check_em(cas_t *cas, em_packet_t *packet)
{
    return cas->__cas.check_em(cas, packet);
}

inline int cas_check_keys(cas_t *cas, em_packet_t *packet)
{
    return cas->__cas.check_keys(cas, packet);
}
