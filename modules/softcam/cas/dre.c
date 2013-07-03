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

#include "../module_cam.h"

struct module_data_t
{
    MODULE_CAS_DATA();

    int is_cas_data_error;

    uint8_t card;
    uint8_t parity;

    int err_id;
    uint8_t pkeys[4]; // previous keys checksum
};

/* cas api */

static bool cas_check_em(module_data_t *mod, mpegts_psi_t *em)
{
    const uint8_t em_type = em->buffer[0];
    switch(em_type)
    {
        // ECM
        case 0x80:
        case 0x81:
        {
            if(em_type != mod->parity)
            {
                mod->parity = em_type;
                return true;
            }
            break;
        }
        // EMM-Unique
        case 0x87:
        case 0x8b:
        {
            if(!memcmp(&em->buffer[3], &mod->__cas.decrypt->cam->ua[4], 4))
               return true;
            break;
        }
        // EMM-Group
        case 0x86:
        case 0x88:
        case 0x89:
        case 0x8c:
        {
            if(em->buffer[3] == mod->__cas.decrypt->cam->ua[4])
                return true;
            break;
        }
        // EMM-Shared
        default:
            break;
    }

    return false;
}

static bool cas_check_keys(module_data_t *mod, const uint8_t *keys)
{
    if(!keys[2])
        return true; // skip, process error in cas.c

    int is_ok = 1;
    if((mod->pkeys[0] == keys[6]) && (mod->pkeys[1] == keys[10]))
    {
        mod->pkeys[2] = keys[14];
        mod->pkeys[3] = keys[18];
        mod->err_id = 0;
    }
    else if((mod->pkeys[2] == keys[14]) && (mod->pkeys[3] == keys[18]))
    {
        mod->pkeys[0] = keys[6];
        mod->pkeys[1] = keys[10];
        mod->err_id = 0;
    }
    else
    {
        if(mod->err_id == 0)
            mod->err_id = 1;
        else if(mod->err_id == 1)
        {
            mod->err_id = 2;
            is_ok = 0;
        }

        mod->pkeys[0] = keys[6];
        mod->pkeys[1] = keys[10];
        mod->pkeys[2] = keys[14];
        mod->pkeys[3] = keys[18];
    }

    return is_ok;
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

static bool cas_check_descriptor(module_data_t *mod, const uint8_t *desc)
{
    if(mod->card)
        return 0;
    const int length = desc[1] - 4;
    if(length < 1)
        return 0;

    /*
     * 11 - Tricolor Center
     * 14 - Tricolor Syberia / Platforma HD new
     * 15 - Platforma HD
     */

    uint8_t dre_id = (length > 0) ? desc[6] : 0;

    uint8_t *cas_data = mod->__cas.decrypt->cas_data;
    if(cas_data[0])
    {
        if(cas_data[0] == dre_id)
            return true;
    }
    else
    {
        int is_prov_ident = 0;

        asc_list_t *prov_list = mod->__cas.decrypt->cam->prov_list;
        asc_list_first(prov_list);
        while(!asc_list_eol(prov_list))
        {
            uint8_t *prov = asc_list_data(prov_list);
            if(prov[2])
            {
                is_prov_ident = 1;
                if(prov[2] == dre_id)
                    return true;
            }
            asc_list_next(prov_list);
        }

        if(!is_prov_ident && !mod->is_cas_data_error)
        {
            asc_log_error("[cas DRE] cas_data is not set");
            mod->is_cas_data_error = 1;
        }
    }

    return 0;
}

static bool cas_check_caid(uint16_t caid)
{
    caid &= ~1;
    return (caid == 0x4AE0 || caid == 0x7BE0);
}

MODULE_CAS(dre)
