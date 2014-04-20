/*
 * Astra Module: SoftCAM
 * http://cesbo.com/astra
 *
 * Copyright (C) 2013, Andrey Dyldin <and@cesbo.com>
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

#ifndef _MODULE_CAM_H_
#define _MODULE_CAM_H_ 1

#include <astra.h>

#define EM_MAX_SIZE 1024

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
    void *arg;
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
    module_data_t *self;

    bool is_ready;

    uint16_t caid;
    uint8_t ua[8];
    bool disable_emm;

    asc_list_t *prov_list;
    asc_list_t *decrypt_list;
    asc_list_t *packet_queue;

    void (*connect)(module_data_t *mod);
    void (*disconnect)(module_data_t *mod);
    void (*send_em)(  module_data_t *mod
                    , module_decrypt_t *decrypt, void *arg
                    , uint8_t *buffer, uint16_t size);
};

#define MODULE_CAM_DATA() module_cam_t __cam

void module_cam_attach_decrypt(module_cam_t *cam, module_decrypt_t *decrypt);
void module_cam_detach_decrypt(module_cam_t *cam, module_decrypt_t *decrypt);

void module_cam_ready(module_cam_t *cam);
void module_cam_reset(module_cam_t *cam);

em_packet_t * module_cam_queue_pop(module_cam_t *cam);
void module_cam_queue_flush(module_cam_t *cam, module_decrypt_t *decrypt);

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

#define module_cam_destroy(_mod)                                                                \
    {                                                                                           \
        module_cam_reset(&_mod->__cam);                                                         \
        for(  asc_list_first(_mod->__cam.decrypt_list)                                          \
            ; !asc_list_eol(_mod->__cam.decrypt_list)                                           \
            ; asc_list_first(_mod->__cam.decrypt_list))                                         \
        {                                                                                       \
            asc_list_remove_current(_mod->__cam.decrypt_list);                                  \
        }                                                                                       \
        asc_list_destroy(_mod->__cam.decrypt_list);                                             \
        asc_list_destroy(_mod->__cam.prov_list);                                                \
        asc_list_destroy(_mod->__cam.packet_queue);                                             \
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
    module_data_t *self;
    module_decrypt_t *decrypt;

    bool (*check_descriptor)(module_data_t *cas_data, const uint8_t *desc);
    bool (*check_em)(module_data_t *cas_data, mpegts_psi_t *em);
    bool (*check_keys)(module_data_t *cas_data, const uint8_t *keys);
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
    module_data_t *self;

    uint16_t pnr;
    uint16_t cas_pnr;
    bool is_cas_data;
    uint8_t cas_data[32];

    module_cam_t *cam;
    module_cas_t *cas;
};

#define MODULE_DECRYPT_DATA() module_decrypt_t __decrypt

void on_cam_ready(module_data_t *mod);
void on_cam_error(module_data_t *mod);
void on_cam_response(module_data_t *mod, void *arg, const uint8_t *data, const char *errmsg);

#endif /* _MODULE_CAM_H_ */
