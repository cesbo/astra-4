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

extern module_cas_t template;

module_cas_t *cas_modules[] =
{
    &template,
    NULL
};

module_cas_t * cas_module_init(uint16_t caid, uint8_t *cas_data)
{
    for(int i = 0; cas_modules[i]; ++i)
    {
        module_cas_t *cas = cas_modules[i]->init(caid, cas_data);
        if(cas)
            return cas;
    }
    return NULL;
}

void cas_module_destroy(module_cas_t *cas)
{
    if(!cas)
        return;

    if(cas->data)
        free(cas->data);
    free(cas);
}
