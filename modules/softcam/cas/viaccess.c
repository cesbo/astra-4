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

struct module_data_t
{
    MODULE_CAS_DATA();

    const uint8_t *ident;
    const uint8_t *sa;

    struct
    {
        uint8_t type; // last shared type
        size_t size;
        uint8_t data[EM_MAX_SIZE]; // ???
    } shared;
};

static int sort_nanos(uint8_t *dest, const uint8_t *src, size_t size)
{
    size_t w = 0;
    int c = -1;
    while(1)
    {
        size_t j = 0;
        int n = 0x100;
        while(j < size)
        {
            size_t l = src[j + 1] + 2;
            if(src[j] == c)
            {
                if(w + l > size)
                    return 0;
                memcpy(dest + w, src + j, l);
                w += l;
            }
            else if (src[j] > c && src[j] < n)
                n = src[j];
            j += l;
        }
        if(n == 0x100)
            break;
        c = n;
    }
    return 1;
}

static inline size_t __emm_size(const uint8_t *payload)
{
    return ((payload[1] & 0x0f) << 8) + payload[2] + 3;
}

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
            return true;
        }
        // shared (part)
        case 0x8C:
        case 0x8D:
        {
            if(!mod->ident)
                break;

            // 3 - shared EMM header size
            const uint8_t *nano = &em->buffer[3];
            if(   (nano[0] != 0x90)
               || (nano[1] != 0x03)
               || (nano[2] != mod->ident[0])
               || (nano[3] != mod->ident[1])
               || ((nano[4] & 0xF0) != (mod->ident[2] & 0xF0)))
            {
                break;
            }

            if(em_type != mod->shared.type)
            {
                const size_t emm_size = __emm_size(em->buffer);
                if(emm_size > sizeof(mod->shared.data))
                {
                    asc_log_warning(  "[softcam/viaccess pnr:%d] EMM nano size is to large"
                                    , mod->__cas.decrypt->cas_pnr);
                    mod->shared.size = 0;
                    break;
                }
                mod->shared.size = emm_size;
                memcpy(mod->shared.data, em->buffer, emm_size);
                mod->shared.type = em_type;
            }
            break;
        }
        case 0x8E:
        {
            if(!mod->shared.size)
                break;
            if(memcmp(&em->buffer[3], &mod->sa[4], 3))
                break;
            if(em->buffer[6] & 0x02)
                break;

            uint8_t emm[EM_MAX_SIZE];
            const size_t emm_size = __emm_size(em->buffer);
            // 7 - unique EMM header size
            const uint8_t *nano = &em->buffer[7];
            const size_t header_size = emm_size - 7;

            size_t size = 0;
            uint8_t unique_size = header_size - 8;
            emm[size++] = 0x9E;
            emm[size++] = unique_size;
            memcpy(&emm[size], nano, unique_size);
            size += unique_size;
            emm[size++] = 0xF0;
            emm[size++] = 0x08;
            memcpy(&emm[size], &nano[unique_size], 8);
            size += 8;

            // 3 - shared EMM header size
            const size_t s_data_size = mod->shared.size - 3;
            if(size + s_data_size > sizeof(emm))
            {
                asc_log_warning(  "[softcam/viaccess pnr:%d] EMM packet size is to large"
                                , mod->__cas.decrypt->cas_pnr);
                mod->shared.size = 0;
                return false;
            }
            memcpy(&emm[size], &mod->shared.data[3], s_data_size);
            size += s_data_size;

            em->buffer[2] = size + 4;
            sort_nanos(&em->buffer[7], emm, size);
            em->buffer_size = PSI_BUFFER_GET_SIZE(em->buffer);

            mod->shared.size = 0;
            return true;
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

static inline int __check_ident(const uint8_t *ident, const uint8_t *prov)
{
    return (   ident[0] == prov[0]
            && ident[1] == prov[1]
            && (ident[2] & 0xF0) == prov[2]);
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
    const int length = desc[1] + 2;
    if(length < 9) // 9 = 6 (desc header) + 3 (viaccess minimal header)
        return (length == 6);

    uint8_t *cas_data = mod->__cas.decrypt->cas_data;
    const bool is_cas_data = (cas_data[0] || cas_data[1]);

    int ident_count = 0;
    const uint8_t *ident = NULL;
    int skip = 6;
    while(skip < length)
    {
        const uint8_t dtype = desc[skip];
        const int dlen = desc[skip + 1] + 2;
        if(dtype == 0x14 && dlen == 5)
        {
            ident = &desc[skip + 2];
            ++ident_count;
            if(!is_cas_data || __check_ident(ident, cas_data))
            {
                asc_list_for(mod->__cas.decrypt->cam->prov_list)
                {
                    const uint8_t *prov = asc_list_data(mod->__cas.decrypt->cam->prov_list);
                    if(__check_ident(ident, prov))
                    {
                        if(!mod->ident)
                        {
                            mod->ident = &prov[0];
                            mod->sa = &prov[3];
                        }
                        return true;
                    }
                }
            }
        }
        skip += dlen;
    }

    return (!ident_count);
}

static bool cas_check_caid(uint16_t caid)
{
    return (caid == 0x0500);
}

MODULE_CAS(viaccess)
