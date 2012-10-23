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

#define LOG_MSG(_msg) "[cam] " _msg

struct cas_data_s
{
    CAS_MODULE_BASE();
};

struct module_data_s
{
    CAM_MODULE_BASE();
};

void cam_queue_flush(module_data_t *mod)
{
    list_t *i = mod->__cam_module.queue.head;
    while(i)
    {
        cam_packet_t *packet = list_get_data(i);
        free(packet);
        i = list_delete(i, NULL);
    }
    mod->__cam_module.queue.head = NULL;
    mod->__cam_module.queue.tail = NULL;
    mod->__cam_module.queue.current = NULL;
    mod->__cam_module.queue.size = 0;
}

static void __remove_item(module_data_t *mod, list_t *i)
{
    cam_packet_t *p = list_get_data(i);
    if(i == mod->__cam_module.queue.head)
    {
        mod->__cam_module.queue.head = list_delete(i, NULL);
        if(!mod->__cam_module.queue.head)
            mod->__cam_module.queue.tail = NULL;
    }
    else if(i == mod->__cam_module.queue.tail)
    {
        mod->__cam_module.queue.tail = list_delete(i, NULL);
    }
    else
        list_delete(i, NULL);

    --mod->__cam_module.queue.size;
    free(p);
}

inline uint16_t cam_caid(module_data_t *mod)
{
    return mod->__cam_module.caid;
}

inline int cam_is_ready(module_data_t *mod)
{
    return mod->__cam_module.is_ready;
}

inline int cam_disable_emm(module_data_t *mod)
{
    return mod->__cam_module.disable_emm;
}

inline void cam_attach_decrypt(module_data_t *mod, module_data_t *decrypt)
{
    if(mod && decrypt)
    {
        mod->__cam_module.decrypts
            = list_insert(mod->__cam_module.decrypts, decrypt);
    }
}

inline void cam_detach_decrypt(module_data_t *mod, module_data_t *decrypt)
{
    if(mod && decrypt)
    {
        mod->__cam_module.decrypts
            = list_delete(mod->__cam_module.decrypts, decrypt);
    }
}

void cam_set_cas_data(module_data_t *mod, const char *data)
{
    memset(mod->__cam_module.cas_data, 0, sizeof(mod->__cam_module.cas_data));
    if(!data)
        return;

    size_t len = strlen(data);
    if(len % 2)
        --len;
    if(len > (sizeof(mod->__cam_module.cas_data) * 2))
        len = (sizeof(mod->__cam_module.cas_data) * 2);
    const int i_max = len / 2;
    str_to_hex(data, mod->__cam_module.cas_data, i_max);
}

int cam_send(module_data_t *mod, cas_data_t *cas, const uint8_t *em)
{
    if(!mod->__cam_module.is_ready)
        return 0;

    list_t *i = NULL;
    mpegts_packet_type_t type = MPEGTS_PACKET_UNKNOWN;

    const uint8_t em_type = em[0];
    if(em_type == 0x80 || em_type == 0x81)
    {
        type = MPEGTS_PACKET_ECM;
        i = mod->__cam_module.queue.head;
    }
    else if(em_type >= 0x82 && em_type <= 0x8F)
    {
        if(CAS2CAM(cas).disable_emm)
            return 0;
        type = MPEGTS_PACKET_EMM;
    }
    else
    {
        log_error(LOG_MSG("wrong packet type 0x%02X"), em_type);
        return 0;
    }

    em = cas->__cas_module.cas->check_em(cas, em);
    if(!em)
        return 0;

    const size_t size = PSI_SIZE(em);
    if(size >= EM_MAX_SIZE)
    {
        log_error(LOG_MSG("entitlement message is too long "
                          "(pnr:%d drop:0x%02X size:%d)")
                  , cas_pnr(cas), em[0], size);
        return 0;
    }

    while(i)
    {
        cam_packet_t *p = list_get_data(i);
        if(p->cas == cas && p->type == MPEGTS_PACKET_ECM)
        {
            if(p->status == 0)
            {
                log_warning(LOG_MSG("drop old packet "
                                    "(pnr:%d drop:0x%02X set:0x%02X)")
                            , cas_pnr(cas), p->payload[0], em[0]);
                __remove_item(mod, i);
                break;
            }
        }
        i = list_get_next(i);
    }

    cam_packet_t *packet = malloc(sizeof(cam_packet_t));
    packet->id = 0;
    packet->cas = cas;
    packet->type = type;
    memcpy(packet->payload, em, size);
    packet->size = size;
    packet->keys[2] = 0x00;
    packet->status = 0;

    if(type == MPEGTS_PACKET_EMM)
        packet->status = 1; // To prevent removing from queue

    ++mod->__cam_module.queue.size;
    mod->__cam_module.queue.tail
        = list_append(mod->__cam_module.queue.tail, packet);
    if(!mod->__cam_module.queue.head)
        mod->__cam_module.queue.head = mod->__cam_module.queue.tail;

    if(!mod->__cam_module.queue.current)
    { // send first packet
        packet->status = 1;
        mod->__cam_module.queue.current = mod->__cam_module.queue.tail;
        cam_module_send_em(mod);
    }

    return 1;
}

int cam_callback(module_data_t *mod, list_t *q_item)
{
    int ret = 1;

    cam_packet_t *packet = list_get_data(q_item);

    packet->status = 2;
    if(packet->type == MPEGTS_PACKET_ECM)
    {
        decrypt_module_set_keys(packet->cas->__cas_module.decrypt
                                , packet->keys);
    }

    mod->__cam_module.queue.current
        = list_get_next(mod->__cam_module.queue.current);

    __remove_item(mod, q_item);

    if(mod->__cam_module.queue.current)
    { // send next packet
        packet = list_get_data(mod->__cam_module.queue.current);
        packet->status = 1;
        cam_module_send_em(mod);
    }

    return ret;
}
