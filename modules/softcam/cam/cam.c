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

cam_packet_t * cam_packet_init(cas_data_t *cas, const uint8_t *em
                               , mpegts_packet_type_t type)
{
    const size_t size = PSI_SIZE(em);
    if(size >= EM_MAX_SIZE)
    {
        log_error(LOG_MSG("entitlement message is too long "
                          "(pnr:%d drop:0x%02X size:%d)")
                  , cas->__cas_module.pnr, em[0], size);
        return NULL;
    }

    cam_packet_t *p = malloc(sizeof(cam_packet_t));
    p->cas = cas;
    p->id = 0;
    p->keys[2] = 0x00;
    memcpy(p->payload, em, size);
    p->size = size;
    p->type = type;
    return p;
}

static void __remove_item(module_data_t *mod, list_t *i)
{
    cam_packet_t *p = list_get_data(i);

    list_t *n = list_delete(i, NULL);

    if(i == mod->__cam_module.queue.head)
    {
        mod->__cam_module.queue.head = n;
        if(!n)
            mod->__cam_module.queue.tail = NULL;
    }
    else if(i == mod->__cam_module.queue.tail)
        mod->__cam_module.queue.tail = n;

    --mod->__cam_module.queue.size;
    free(p);
}

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
    mod->__cam_module.queue.size = 0;
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
        list_t *i = mod->__cam_module.queue.head;
        while(i)
        {
            list_t *n = list_get_next(i);
            cam_packet_t *packet = list_get_data(i);
            if(packet->cas->__cas_module.decrypt == decrypt)
                __remove_item(mod, i);
            i = n;
        }

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

    list_t *i = NULL; // check ECM packet in queue

    const uint8_t em_type = em[0];
    if(em_type == 0x80 || em_type == 0x81)
    {
        if(mod->__cam_module.queue.head) // skip head, it processed by cam
            i = list_get_next(mod->__cam_module.queue.head);
    }
    else if(em_type >= 0x82 && em_type <= 0x8F)
    {
        if(mod->__cam_module.disable_emm)
            return 0;
    }
    else
    {
        log_error(LOG_MSG("wrong packet type 0x%02X"), em_type);
        return 0;
    }

    cam_packet_t *packet = cas->__cas_module.cas->check_em(cas, em);
    if(!packet)
        return 0;

    while(i)
    {
        cam_packet_t *p = list_get_data(i);
        if(p->type == MPEGTS_PACKET_ECM && p->cas == cas)
        {
            log_warning(LOG_MSG("drop old packet "
                                "(pnr:%d drop:0x%02X set:0x%02X)")
                        , cas->__cas_module.pnr, p->payload[0], em[0]);
            __remove_item(mod, i);
            break;
        }
        i = list_get_next(i);
    }

    ++mod->__cam_module.queue.size;
    mod->__cam_module.queue.tail
        = list_append(mod->__cam_module.queue.tail, packet);
    if(!mod->__cam_module.queue.head)
    {
        mod->__cam_module.queue.head = mod->__cam_module.queue.tail;
        cam_module_send_em(mod); // initial sending
    }

    return 1;
}

static int check_keys(cam_packet_t *packet)
{
    const uint8_t *keys = packet->keys;
    do
    {
        if(!packet->cas->__cas_module.cas->check_keys(packet))
            break;

        if(keys[2] != 16)
            break;

        if((keys[0] & (~1)) != 0x80)
            break;

        const uint8_t ck1 = (keys[3] + keys[4] + keys[5]) & 0xFF;
        if(ck1 != keys[6])
            break;

        const uint8_t ck2 = (keys[7] + keys[8] + keys[9]) & 0xFF;
        if(ck2 != keys[10])
            break;

        return 1;
    } while(0);

    return 0;
}

int cam_callback(module_data_t *mod, cam_packet_t *packet)
{
    int ret = 1;

    if(packet->type == MPEGTS_PACKET_ECM)
    {
        const int is_ok = check_keys(packet);
        decrypt_module_set_keys(packet->cas->__cas_module.decrypt
                                , packet->keys, is_ok);
    }

    __remove_item(mod, mod->__cam_module.queue.head);

    if(mod->__cam_module.queue.size)
        cam_module_send_em(mod);

    return ret;
}
