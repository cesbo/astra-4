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

#ifndef _SOFTCAM_H_
#define _SOFTCAM_H_ 1

#include <astra.h>
#include <modules/mpegts/mpegts.h>

/*
 * ECM decrypt
 *  -> cam_send
 *   -> check_em
 *   -> cam_module_send_em (interface_send_em)
 *    -> cam_callback
 *     -> cam_check_keys
 *     -> interface_set_keys
 */

#define EM_MAX_SIZE 400

typedef struct cas_data_s cas_data_t;

typedef struct
{
    int id;
    cas_data_t *cas;

    mpegts_packet_type_t type;
    uint8_t payload[EM_MAX_SIZE];
    size_t size;

    uint8_t keys[19];

/* status:
 * 0 - in queue
 * 1 - awaiting response
 * 2 - out of date
 */
    int status;
} cam_packet_t;

/* Conditional Access System */

typedef struct
{
    const char *name;
    int (*check_caid)(uint16_t);
    uint16_t (*check_desc)(cas_data_t *, const uint8_t *);
    const uint8_t * (*check_em)(cas_data_t *, const uint8_t *);
    int (*check_keys)(cas_data_t *, const uint8_t *);
    size_t datasize;
} cas_module_t;

#define CAS_MODULE(_name)                                                   \
const cas_module_t cas_module_##_name =                                     \
{                                                                           \
    .name = #_name                                                          \
    , .check_caid = _name##_check_caid                                      \
    , .check_desc = _name##_check_desc                                      \
    , .check_em = _name##_check_em                                          \
    , .check_keys = _name##_check_keys                                      \
    , .datasize = sizeof(cas_data_t)                                        \
}

#define CAS_MODULE_BASE()                                                   \
struct                                                                      \
{                                                                           \
    const cas_module_t *cas;                                                \
    module_data_t *decrypt;                                                 \
    module_data_t *cam;                                                     \
    uint16_t pnr;                                                           \
} __cas_module

#define CA_DESC_CAID(_desc) ((_desc[2] << 8) | _desc[3])
#define CA_DESC_PID(_desc) (((_desc[4] & 0x1F) << 8) | _desc[5])

cas_data_t * cas_init(module_data_t *, module_data_t *, uint16_t);
void cas_destroy(cas_data_t *);

const char * cas_name(cas_data_t *);
uint16_t cas_pnr(cas_data_t *);
int cas_check_keys(cas_data_t *, const uint8_t *);
uint16_t cas_check_descriptor(cas_data_t *, const uint8_t *);

/* Conditional Access Module */

#define CAM_MODULE_BASE()                                                   \
MODULE_BASE();                                                              \
struct                                                                      \
{                                                                           \
    const char *name;                                                       \
    uint16_t caid;                                                          \
    uint8_t ua[8];                                                          \
    list_t *prov_list;                                                      \
    int is_ready;                                                           \
    int disable_emm;                                                        \
    struct                                                                  \
    {                                                                       \
        size_t size;                                                        \
        list_t *head;                                                       \
        list_t *tail;                                                       \
        list_t *current;                                                    \
    } queue;                                                                \
    uint8_t cas_data[32];                                                   \
    list_t *decrypts;                                                       \
} __cam_module

#define CAS2CAM(_cas) _cas->__cas_module.cam->__cam_module

uint16_t cam_caid(module_data_t *);
int cam_is_ready(module_data_t *);
int cam_disable_emm(module_data_t *);

void cam_attach_decrypt(module_data_t *, module_data_t *);
void cam_detach_decrypt(module_data_t *, module_data_t *);

void cam_set_cas_data(module_data_t *, const char *);

void cam_queue_flush(module_data_t *);

int cam_callback(module_data_t *, list_t *);
int cam_send(module_data_t *, cas_data_t *, const uint8_t *);

/* CAM-module interface */

#define CAM_INTERFACE()                                                     \
{                                                                           \
    MODULE_INTERFACE(0x4, interface_send_em);                               \
}

#define cam_module_send_em(_mod)                                            \
    _mod->__interface[0x4](_mod)

/* Decrypt-module interface */

#define DECRYPT_INTERFACE()                                                 \
{                                                                           \
    MODULE_INTERFACE(0x5, interface_set_keys);                              \
    MODULE_INTERFACE(0x6, interface_cam_status);                            \
}

#define decrypt_module_set_keys(_mod, _keys)                                \
    _mod->__interface[0x5](_mod, _keys)

/*
 * status:
 * -1 - cam-module was destroyed
 *  0 - down
 *  1 - ready
 */

#define decrypt_module_cam_status(_mod, _status)                            \
{                                                                           \
    _mod->__cam_module.is_ready = _status;                                  \
    list_t *__i = list_get_first(_mod->__cam_module.decrypts);              \
    while(__i)                                                              \
    {                                                                       \
        module_data_t *__decrypt = list_get_data(__i);                      \
        __decrypt->__interface[0x6](__decrypt, _status);                    \
        __i = list_get_next(__i);                                           \
    }                                                                       \
}

#endif /* _SOFTCAM_H_ */
