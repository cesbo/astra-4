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

    module_data_t *self;
};

#define MODULE_CAM_DATA() module_cam_t __cam

#define module_cam_set_provider(_mod, _provider)                                                \
    asc_list_insert_tail(_mod->__cam.prov_list, _provider);

#define module_cam_reset(_mod)                                                                  \
    {                                                                                           \
        for(asc_list_first(mod->__cam.prov_list)                                                \
            ; !asc_list_eol(mod->__cam.prov_list)                                               \
            ; asc_list_first(mod->__cam.prov_list))                                             \
        {                                                                                       \
            asc_list_remove_current(mod->__cam.prov_list);                                      \
        }                                                                                       \
    }

#define module_cam_init(_mod)                                                                   \
    {                                                                                           \
        _mod->__cam.self = _mod;                                                                \
        _mod->__cam.decrypt_list = asc_list_init();                                             \
        _mod->__cam.prov_list = asc_list_init();                                                \
    }

#define module_cam_destroy(_mod)                                                                \
    {                                                                                           \
        module_cam_reset(_mod);                                                                 \
        for(asc_list_first(_mod->__cam.decrypt_list)                                            \
            ; !asc_list_eol(_mod->__cam.decrypt_list)                                           \
            ; asc_list_first(_mod->__cam.decrypt_list))                                         \
        {                                                                                       \
            module_decrypt_t *__decrypt = asc_list_data(_mod->__cam.decrypt_list);              \
            __decrypt->on_cam_error(__decrypt->self);                                           \
            asc_list_remove_current(_mod->__cam.decrypt_list);                                  \
        }                                                                                       \
        asc_list_destroy(_mod->__cam.decrypt_list);                                             \
        asc_list_destroy(_mod->__cam.prov_list);                                                \
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

typedef struct cas_data_t cas_data_t;

struct module_cas_t
{
    module_cas_t * (*init)(uint16_t caid, const uint8_t *cas_data);
    bool (*check_descriptor)(module_cas_t *cas, const uint8_t *desc);
    bool (*check_em)(module_cas_t *cas, mpegts_psi_t *em);
    bool (*check_key)(module_cas_t *cas, uint8_t parity, const uint8_t *key);

    cas_data_t *data;
};

module_cas_t * cas_module_init(uint16_t caid, uint8_t *cas_data);
void cas_module_destroy(module_cas_t *cas);

#define cas_module_check_descriptor(_cas, _desc) _cas->check_descriptor(_cas, _desc)
#define cas_module_check_em(_cas, _em) _cas->check_em(_cas, _em)
#define cas_module_check_key(_cas, _parity, _key) _cas->check_key(_cas, _parity, _key)

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

    module_cam_t *cam;
    module_cas_t *cas;

    void (*on_cam_ready)(module_data_t *mod);
    void (*on_cam_error)(module_data_t *mod);
    void (*on_key)(module_data_t *mod, uint8_t parity, uint8_t *key);

    module_data_t *self;
};

#define MODULE_DECRYPT_DATA() module_decrypt_t __decrypt

#endif /* _MODULE_CAM_H_ */
