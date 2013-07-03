/*
 * Astra SoftCAM Module
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
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
 */

#ifndef _MODULE_CAM_H_
#define _MODULE_CAM_H_ 1

#include <astra.h>

#define EM_MAX_SIZE 512

typedef struct module_decrypt_t module_decrypt_t;
typedef struct module_cam_t module_cam_t;
typedef struct module_cas_t module_cas_t;

typedef struct em_packet_t em_packet_t;

/*
 * oooooooooo   o       oooooooo8 oooo   oooo ooooooooooo ooooooooooo
 *  888    888 888    o888     88  888  o88    888    88  88  888  88
 *  888oooo88 8  88   888          888888      888ooo8        888
 *  888      8oooo88  888o     oo  888  88o    888    oo      888
 * o888o   o88o  o888o 888oooo88  o888o o888o o888ooo8888    o888o
 *
 */

struct em_packet_t
{
    uint8_t buffer[EM_MAX_SIZE];
    uint16_t buffer_size;

    module_decrypt_t *decrypt;
};

/*
 *   oooooooo8     o      oooo     oooo
 * o888     88    888      8888o   888
 * 888           8  88     88 888o8 88
 * 888o     oo  8oooo88    88  888  88
 *  888oooo88 o88o  o888o o88o  8  o88o
 *
 */

struct module_cam_t
{
    int is_ready;

    uint16_t caid;
    uint8_t ua[8];
    int disable_emm;

    asc_list_t *prov_list;
    asc_list_t *decrypt_list;
    asc_list_t *packet_queue;

    void (*connect)(module_data_t *mod);
    void (*disconnect)(module_data_t *mod);
    void (*send_em)(module_data_t *mod, module_decrypt_t *decrypt, uint8_t *buffer, uint16_t size);

    module_data_t *self;
};

#define MODULE_CAM_DATA() module_cam_t __cam

em_packet_t * __module_cam_queue_pop(module_cam_t *cam);
#define module_cam_queue_pop(_mod) __module_cam_queue_pop(&_mod->__cam)

void module_cam_queue_flush(module_cam_t *cam, module_decrypt_t *decrypt);

#define module_cam_set_provider(_mod, _provider)                                                \
    asc_list_insert_tail(_mod->__cam.prov_list, _provider)

void __module_cam_ready(module_cam_t *cam);
#define module_cam_ready(_mod) __module_cam_ready(&_mod->__cam)

void __module_cam_reset(module_cam_t *cam);
#define module_cam_reset(_mod) __module_cam_reset(&_mod->__cam)

#define module_cam_response(_mod)                                                               \
    _mod->packet->decrypt->on_response(_mod->packet->decrypt->self, _mod->packet->buffer)

#define module_cam_init(_mod, _connect, _disconnect, _send_em)                                  \
    {                                                                                           \
        _mod->__cam.self = _mod;                                                                \
        _mod->__cam.decrypt_list = asc_list_init();                                             \
        _mod->__cam.prov_list = asc_list_init();                                                \
        _mod->__cam.packet_queue = asc_list_init();                                             \
        _mod->__cam.connect = _connect;                                                         \
        _mod->__cam.disconnect = _disconnect;                                                   \
        _mod->__cam.send_em = _send_em;                                                         \
    }

void __module_cam_destroy(module_cam_t *cam);
#define module_cam_destroy(_mod) __module_cam_destroy(&_mod->__cam)

#define module_cam_attach_decrypt(_cam, _decrypt)                                               \
    {                                                                                           \
        asc_list_first(_cam->decrypt_list);                                                     \
        if(asc_list_eol(_cam->decrypt_list))                                                    \
            _cam->connect(_cam->self);                                                          \
        asc_list_insert_tail(_cam->decrypt_list, _decrypt);                                     \
        if(_cam->is_ready)                                                                      \
            (_decrypt)->on_cam_ready((_decrypt)->self);                                         \
    }

#define module_cam_detach_decrypt(_cam, _decrypt)                                               \
    {                                                                                           \
        module_cam_queue_flush(_cam, _decrypt);                                                 \
        asc_list_remove_item(_cam->decrypt_list, _decrypt);                                     \
        asc_list_first(_cam->decrypt_list);                                                     \
        if(asc_list_eol(_cam->decrypt_list))                                                    \
            _cam->disconnect(_cam->self);                                                       \
    }

#define MODULE_CAM_METHODS()                                                                    \
    static int module_cam_cam(module_data_t *mod)                                               \
    {                                                                                           \
        lua_pushlightuserdata(lua, &mod->__cam);                                                \
        return 1;                                                                               \
    }

#define MODULE_CAM_METHODS_REF()                                                                \
    { "cam", module_cam_cam }

/*
 *   oooooooo8     o       oooooooo8
 * o888     88    888     888
 * 888           8  88     888oooooo
 * 888o     oo  8oooo88           888
 *  888oooo88 o88o  o888o o88oooo888
 *
 */

struct module_cas_t
{
    module_decrypt_t *decrypt;

    bool (*check_descriptor)(module_data_t *cas_data, const uint8_t *desc);
    bool (*check_em)(module_data_t *cas_data, mpegts_psi_t *em);
    bool (*check_keys)(module_data_t *cas_data, const uint8_t *keys);

    module_data_t *self;
};

#define MODULE_CAS_DATA() module_cas_t __cas

#define module_cas_check_descriptor(_cas, _desc) _cas->check_descriptor(_cas->self, _desc)
#define module_cas_check_em(_cas, _em) _cas->check_em(_cas->self, _em)
#define module_cas_check_keys(_cas, _keys) _cas->check_keys(_cas->self, _keys)

#define MODULE_CAS(_name)                                                                       \
    module_cas_t * _name##_cas_init(module_decrypt_t *decrypt)                                  \
    {                                                                                           \
        if(!cas_check_caid(decrypt->cam->caid)) return NULL;                                    \
        module_data_t *mod = calloc(1, sizeof(module_data_t));                                  \
        mod->__cas.self = mod;                                                                  \
        mod->__cas.decrypt = decrypt;                                                           \
        mod->__cas.check_descriptor = cas_check_descriptor;                                     \
        mod->__cas.check_em = cas_check_em;                                                     \
        mod->__cas.check_keys = cas_check_keys;                                                 \
        return &mod->__cas;                                                                     \
    }

/*
 * ooooooooo  ooooooooooo  oooooooo8 oooooooooo ooooo  oooo oooooooooo  ooooooooooo
 *  888    88o 888    88 o888     88  888    888  888  88    888    888 88  888  88
 *  888    888 888ooo8   888          888oooo88     888      888oooo88      888
 *  888    888 888    oo 888o     oo  888  88o      888      888            888
 * o888ooo88  o888ooo8888 888oooo88  o888o  88o8   o888o    o888o          o888o
 *
 */

struct module_decrypt_t
{
    uint16_t pnr;
    uint8_t cas_data[32];

    module_cam_t *cam;
    module_cas_t *cas;

    void (*on_cam_ready)(module_data_t *mod);
    void (*on_cam_error)(module_data_t *mod);
    void (*on_response)(module_data_t *mod, const uint8_t *data);

    module_data_t *self;
};

#define MODULE_DECRYPT_DATA() module_decrypt_t __decrypt

#endif /* _MODULE_CAM_H_ */
