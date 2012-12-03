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

struct module_data_s
{
    CAM_MODULE_BASE();
};

struct cas_data_s
{
    CAS_MODULE_BASE();

    uint8_t parity;

    const uint8_t *ident;
    const uint8_t *sa;

    struct
    {
        uint8_t type; // last shared type
        size_t size;
        uint8_t data[128]; // ???
    } shared;

    uint8_t emm[512];
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

static int viaccess_check_caid(uint16_t caid)
{
    return (caid == 0x0500);
}

static cam_packet_t * viaccess_check_em(cas_data_t *cas
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
        // shared (part)
        case 0x8C:
        case 0x8D:
        {
            // 3 - shared EMM header size
            const uint8_t *nano = &payload[3];
            if(!(nano[0] == 0x90 && nano[1] == 0x03
                 && (nano[2] == cas->ident[0])
                 && (nano[3] == cas->ident[1])
                 && ((nano[4] & 0xF0) == (cas->ident[2] & 0xF0))))
            {
                break;
            }

            if(em_type != cas->shared.type)
            {
                const size_t emm_size = __emm_size(payload);
                cas->shared.size = emm_size;
                if(emm_size > sizeof(cas->shared.data))
                {
                    log_error("[cas Viaccess] please report to "
                              "and@cesbo.com: bug:234526062012:%d"
                              , emm_size);
                    break;
                }
                memcpy(cas->shared.data, payload, emm_size);
                cas->shared.type = em_type;
            }
            break;
        }
        case 0x8E:
        {
            if(!cas->shared.size)
                break;
            if(memcmp(&payload[3], &cas->sa[4], 3))
                break;
            if(payload[6] & 0x02)
                break;

            uint8_t emm[512];
            const size_t emm_size = __emm_size(payload);
            // 7 - unique EMM header size
            const uint8_t *nano = &payload[7];
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
            const size_t s_data_size = cas->shared.size - 3;
            memcpy(&emm[size], &cas->shared.data[3], s_data_size);
            size += s_data_size;

            memcpy(cas->emm, payload, 7);
            cas->emm[2] = size + 4;
            sort_nanos(&cas->emm[7], emm, size);

            return cam_packet_init(cas, cas->emm, MPEGTS_PACKET_EMM);
        }
        default:
            break;
    }

    return NULL;
}

static int viaccess_check_keys(cam_packet_t *packet)
{
    return 1;
}

inline static int __check_ident(const uint8_t *ident, const uint8_t *prov)
{
    return (ident[0] == prov[0]
            && ident[1] == prov[1]
            && (ident[2] & 0xF0) == prov[2]);
}

static uint16_t viaccess_check_desc(cas_data_t *cas, const uint8_t *desc)
{
    const int length = desc[1] + 2;
    if(length < 9) // 9 = 6 (desc header) + 3 (viaccess minimal header)
        return 0;

    uint8_t *cas_data = CAS2CAM(cas).cas_data;
    const int is_cas_data = (cas_data[0] || cas_data[1]);

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
            if((!is_cas_data)
               || (is_cas_data && __check_ident(ident, cas_data)))
            {
                list_t *i = list_get_first(CAS2CAM(cas).prov_list);
                while(i)
                {
                    const uint8_t *prov = list_get_data(i);
                    if(__check_ident(ident, prov))
                    {
                        if(!cas->ident)
                        {
                            cas->ident = &prov[0];
                            cas->sa = &prov[3];
                        }
                        return CA_DESC_PID(desc);
                    }
                    i = list_get_next(i);
                }
            }
        }
        skip += dlen;
    }

    if(!ident_count)
        return CA_DESC_PID(desc);

    return 0;
}

CAS_MODULE(viaccess);
