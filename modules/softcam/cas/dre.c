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

/* to access to the cam module information */
struct module_data_s
{
    CAM_MODULE_BASE();
};

struct cas_data_s
{
    CAS_MODULE_BASE();

    int is_cas_data_error;

    uint8_t card;
    uint8_t parity;

    int err_id;
    uint8_t pkeys[4]; // previous keys checksum
};

/* cas api */

static int dre_check_caid(uint16_t caid)
{
    caid &= ~1;
    return (caid == 0x4AE0 || caid == 0x7BE0);
}

static cam_packet_t * dre_check_em(cas_data_t *cas
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
                return cam_packet_init(cas, payload, MPEGTS_PACKET_ECM);
            }
            break;
        }
        // EMM-Unique
        case 0x87:
        case 0x8b:
        {
            if(!memcmp(&payload[3], &CAS2CAM(cas).ua[4], 4))
               return cam_packet_init(cas, payload, MPEGTS_PACKET_EMM);
            break;
        }
        // EMM-Group
        case 0x86:
        case 0x88:
        case 0x89:
        case 0x8c:
        {
            if(payload[3] == CAS2CAM(cas).ua[4])
                return cam_packet_init(cas, payload, MPEGTS_PACKET_EMM);
            break;
        }
        // EMM-Shared
        default:
            break;
    }

    return NULL;
}

static int dre_check_keys(cam_packet_t *packet)
{
    cas_data_t *cas = packet->cas;
    const uint8_t *keys = packet->keys;
    if(!keys[2])
        return 1; // skip, process error in cas.c

    int is_ok = 1;
    if((cas->pkeys[0] == keys[6]) && (cas->pkeys[1] == keys[10]))
    {
        cas->pkeys[2] = keys[14];
        cas->pkeys[3] = keys[18];
        cas->err_id = 0;
    }
    else if((cas->pkeys[2] == keys[14]) && (cas->pkeys[3] == keys[18]))
    {
        cas->pkeys[0] = keys[6];
        cas->pkeys[1] = keys[10];
        cas->err_id = 0;
    }
    else
    {
        if(cas->err_id == 0)
            cas->err_id = 1;
        else if(cas->err_id == 1)
        {
            cas->err_id = 2;
            is_ok = 0;
        }

        cas->pkeys[0] = keys[6];
        cas->pkeys[1] = keys[10];
        cas->pkeys[2] = keys[14];
        cas->pkeys[3] = keys[18];
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

static uint16_t dre_check_desc(cas_data_t *cas, const uint8_t *desc)
{
    if(cas->card)
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

    if(CAS2CAM(cas).cas_data[0])
    {
        if(CAS2CAM(cas).cas_data[0] == dre_id)
            return CA_DESC_PID(desc);
    }
    else
    {
        int is_prov_ident = 0;

        list_t *i = list_get_first(CAS2CAM(cas).prov_list);
        while(i)
        {
            uint8_t *prov = list_get_data(i);
            if(prov[2])
            {
                is_prov_ident = 1;
                if(prov[2] == dre_id)
                    return CA_DESC_PID(desc);
            }
            i = list_get_next(i);
        }

        if(!is_prov_ident && !cas->is_cas_data_error)
        {
            log_error("[cas DRE] cas_data is not set");
            cas->is_cas_data_error = 1;
        }
    }

    return 0;
}

CAS_MODULE(dre);
