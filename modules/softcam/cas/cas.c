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
#include <modules/utils/utils.h>

extern const cas_module_t cas_module_biss
                        , cas_module_viaccess
                        , cas_module_dre
                        , cas_module_irdeto
                        , cas_module_conax
                        , cas_module_mediaguard
                        , cas_module_nagra
                        , cas_module_bulcrypt
                        , cas_module_cryptoworks
                        , cas_module_videoguard
                        ;

static const cas_module_t *cas_module_list[] =
{
    &cas_module_biss
    , &cas_module_viaccess
    , &cas_module_dre
    , &cas_module_irdeto
    , &cas_module_conax
    , &cas_module_mediaguard
    , &cas_module_nagra
    , &cas_module_bulcrypt
    , &cas_module_cryptoworks
    , &cas_module_videoguard
};

struct cas_data_s
{
    CAS_MODULE_BASE();
};

struct module_data_s
{
    CAM_MODULE_BASE();
};

cas_data_t * cas_init(module_data_t *decrypt, module_data_t *cam, uint16_t pnr)
{
    const cas_module_t *cas_mod = NULL;

    for(int i = 0; i < ARRAY_SIZE(cas_module_list); ++i)
    {
        cas_mod = cas_module_list[i];
        if(cas_mod->check_caid(cam->__cam_module.caid))
            break;
        cas_mod = NULL;
    }
    if(!cas_mod)
        return NULL;

    cas_data_t *cas = calloc(1, cas_mod->datasize);
    cas->__cas_module.cas = cas_mod;
    cas->__cas_module.decrypt = decrypt;
    cas->__cas_module.cam = cam;
    cas->__cas_module.pnr = pnr;

    return (cas_data_t *)cas;
}

void cas_destroy(cas_data_t *cas)
{
    cam_queue_flush(cas->__cas_module.cam, cas);

    if(cas)
        free(cas);
}

inline const char * cas_name(cas_data_t *cas)
{
    return cas->__cas_module.cas->name;
}

uint16_t cas_check_descriptor(cas_data_t *cas, const uint8_t *desc)
{
    if(desc[0] != 0x09)
        return 0;
    if(CA_DESC_CAID(desc) != CAS2CAM(cas).caid)
        return 0;
    return cas->__cas_module.cas->check_desc(cas, desc);
}
