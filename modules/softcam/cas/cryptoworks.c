/*
 * Astra Module: SoftCAM
 * http://cesbo.com/en/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 *               2011, Santa77 <santa77@fibercom.sk>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "../module_cam.h"

struct module_data_t
{
    MODULE_CAS_DATA();

    uint8_t parity;
};

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
        case 0x82:
        { // unique
            if(em->buffer[3] == 0xA9 && em->buffer[4] == 0xFF
               && em->buffer[13] == 0x80 && em->buffer[14] == 0x05
               && !memcmp(&em->buffer[5], &mod->__cas.decrypt->cam->ua[3], 5))
            {
                return true;
            }
            break;
        }
        case 0x84:
        { // shared
            if(em->buffer[3] == 0xA9 && em->buffer[4] == 0xFF
               && em->buffer[12] == 0x80 && em->buffer[13] == 0x04
               && !memcmp(&em->buffer[5], &mod->__cas.decrypt->cam->ua[3], 4))
            {
                return true;
            }
            break;
        }
        case 0x86:
        { // global
            if(em->buffer[3] == 0xA9 && em->buffer[4] == 0xFF
               && em->buffer[5] == 0x83 && em->buffer[6] == 0x01
               && em->buffer[8] == 0x85) /* em->buffer[8] == 0x84 is not known */
            {
                return true;
            }
            break;
        }
        case 0x88:
        case 0x89:
        { // global
            if(em->buffer[3] == 0xA9 && em->buffer[4] == 0xFF
               && em->buffer[8] == 0x83 && em->buffer[9] == 0x01)
            {
                return true;
            }
            break;
        }
        default:
            break;
    }

    return false;
}

static bool cas_check_keys(module_data_t *mod, const uint8_t *keys)
{
    __uarg(mod);
    __uarg(keys);
    return true;
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
    __uarg(mod);
    __uarg(desc);
    return true;
}

static bool cas_check_caid(uint16_t caid)
{
    return ((caid & 0xFF00) == 0x0D00);
}

MODULE_CAS(cryptoworks)
