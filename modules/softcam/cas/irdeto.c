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

#define ECM_MAX_ID 16

/* to access to the cam module information */
struct module_data_s
{
    CAM_MODULE_BASE();
};

struct cas_data_s
{
    CAS_MODULE_BASE();

    uint8_t parity;

    // autoselect chid
    struct
    {
        int is_checking;
        uint8_t current_id;
        struct
        {
            uint8_t parity;
            uint16_t chid;
        } ecm_id[ECM_MAX_ID];
    } test;

    uint16_t chid; // selected channel id

    uint8_t *sa;
};

/* cas api */

static int irdeto_check_caid(uint16_t caid)
{
    return ((caid & 0xFF00) == 0x0600);
}

inline static uint16_t irdeto_ecm_chid(const uint8_t *payload)
{
    return (payload[6] << 8) | payload[7];
}

static int irdeto_check_ecm(cas_data_t *cas, const uint8_t *payload)
{
    const uint8_t parity = payload[0];
    if(parity == cas->parity)
        return 0;

    const uint16_t chid = irdeto_ecm_chid(payload);
    if(cas->chid)
    {
        if(cas->chid != chid)
            return 0;
        cas->parity = parity;
        return 1;
    }

    /* autoselect ecm chid */
    if(cas->test.is_checking)
        return 0;

    const uint8_t ecm_id = payload[4];
    if(ecm_id >= ECM_MAX_ID)
        return 0;

    if(cas->test.ecm_id[ecm_id].parity == parity)
        return 0;

    cas->test.is_checking = 1;
    cas->test.current_id = ecm_id;
    cas->test.ecm_id[ecm_id].parity = parity;
    cas->test.ecm_id[ecm_id].chid = chid;

    return 1;
}

static const uint8_t * irdeto_check_em(cas_data_t *cas
                                       , const uint8_t *payload)
{
    const uint8_t em_type = payload[0];
    switch(em_type)
    {
        // ECM
        case 0x80:
        case 0x81:
        {
            if(irdeto_check_ecm(cas, payload))
                return payload;
            break;
        }
        // EMM
        default:
        {
            const uint8_t emm_len = payload[3] & 0x07;
            const uint8_t emm_base = payload[3] >> 3;
            uint8_t *a = (emm_base & 0x10)
                       ? CAS2CAM(cas).ua        // check card
                       : cas->sa;               // check provider
            if(emm_base == a[4]
               && (!emm_len || !memcmp(&payload[4], &a[5], emm_len)))
            {
                return payload;
            }
            break;
        }
    }

    return NULL;
}

static int irdeto_check_keys(cas_data_t *cas, const uint8_t *keys)
{
    if(!keys[2])
    {
        if(!cas->chid)
            cas->test.is_checking = 0;
        return 0;
    }

    if(!cas->chid)
    {
        /* cas->test_count always greater than 0,
           because increased in irdeto_check_ecm */
        cas->chid = cas->test.ecm_id[cas->test.current_id].chid;
        cas->parity = keys[0];
        log_info("[cas Irdeto pnr:%d] select chid:0x%04X"
                 , cas_pnr(cas), cas->chid);
    }

    return 1;
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

static uint16_t irdeto_check_desc(cas_data_t *cas, const uint8_t *desc)
{
    if(!cas->sa)
    {
        list_t *i = list_get_first(CAS2CAM(cas).prov_list);
        cas->sa = list_get_data(i);
        cas->sa = &cas->sa[3];

        uint8_t *cas_data = CAS2CAM(cas).cas_data;
        if(cas_data[1])
            cas->chid = (cas_data[0] << 8) | cas_data[1];
    }

    return CA_DESC_PID(desc);
}

CAS_MODULE(irdeto);
