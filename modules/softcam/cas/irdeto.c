/*
 * Astra Module: SoftCAM
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
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

#define ECM_MAX_ID 16

struct module_data_t
{
    MODULE_CAS_DATA();

    // autoselect chid
    struct
    {
        bool is_checking;
        uint8_t current_id;
        struct
        {
            int check;
            uint16_t chid;
        } ecm_id[ECM_MAX_ID];
    } test;

    int is_chid;
    uint16_t chid; // selected channel id

    uint8_t *ua;
    uint8_t *sa;
};

/* cas api */

inline static uint16_t irdeto_ecm_chid(const uint8_t *payload)
{
    return (payload[6] << 8) | payload[7];
}

static bool irdeto_check_ecm(module_data_t *mod, const uint8_t *payload)
{
    const uint16_t chid = irdeto_ecm_chid(payload);
    if(mod->is_chid != 0)
        return (mod->chid == chid);

    /* autoselect ecm chid */
    if(mod->test.is_checking)
        return false;

    const uint8_t ecm_id = payload[4];
    if(ecm_id >= ECM_MAX_ID)
        return false;

    int max_check = 0;
    int min_check = 0;
    for(int i = 0; i < ECM_MAX_ID; ++i)
    {
        const int ecm_check = mod->test.ecm_id[i].check;
        if(ecm_check > max_check)
            max_check = ecm_check;
        if(ecm_check < min_check)
            min_check = ecm_check;
    }

    if(max_check == min_check)
        memset(&mod->test.ecm_id, 0, sizeof(mod->test.ecm_id));

    if(max_check != min_check && mod->test.ecm_id[ecm_id].check >= max_check)
        return false;

    mod->test.is_checking = true;
    mod->test.current_id = ecm_id;
    ++mod->test.ecm_id[ecm_id].check;
    mod->test.ecm_id[ecm_id].chid = chid;

    return true;
}

static bool cas_check_em(module_data_t *mod, mpegts_psi_t *em)
{
    const uint8_t em_type = em->buffer[0];
    switch(em_type)
    {
        // ECM
        case 0x80:
        case 0x81:
        {
            if(irdeto_check_ecm(mod, em->buffer))
                return true;
            break;
        }
        // EMM
        default:
        {
            const uint8_t emm_len = em->buffer[3] & 0x07;
            const uint8_t emm_base = em->buffer[3] >> 3;
            uint8_t *a = (emm_base & 0x10)
                       ? mod->ua    // check card
                       : mod->sa;   // check provider
            if(a
               && emm_base == a[4]
               && (!emm_len || !memcmp(&em->buffer[4], &a[5], emm_len)))
            {
                return true;
            }
            break;
        }
    }

    return false;
}

static bool cas_check_keys(module_data_t *mod, const uint8_t *keys)
{
    if(!keys[2])
    {
        mod->test.is_checking = false;
        if(mod->is_chid == 1)
            mod->is_chid = 0;

        return false;
    }

    if(!mod->is_chid)
    {
        mod->is_chid = 1;
        /* cas->test_count always greater than 0,
           because increased in irdeto_check_ecm */
        mod->chid = mod->test.ecm_id[mod->test.current_id].chid;
        asc_log_info("[cas Irdeto PNR:%d] select chid:0x%04X", mod->__cas.decrypt->pnr, mod->chid);
    }

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
    __uarg(desc);

    if(!mod->sa)
    {
        asc_list_first(mod->__cas.decrypt->cam->prov_list);
        mod->sa = asc_list_data(mod->__cas.decrypt->cam->prov_list);
        mod->sa = &mod->sa[3];
        mod->ua = mod->__cas.decrypt->cam->ua;

        if(mod->__cas.decrypt->is_cas_data)
        {
            mod->is_chid = 2;
            mod->chid = (mod->__cas.decrypt->cas_data[0] << 8) | mod->__cas.decrypt->cas_data[1];
        }
    }

    return true;
}

static bool cas_check_caid(uint16_t caid)
{
    return (((caid & 0xFF00) == 0x0600) || (caid == 0x1702));
}

MODULE_CAS(irdeto)
