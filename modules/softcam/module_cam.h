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

/*
 * Initializing:
 * 1) decrypt: set cam
 * 1) decrypt: on_pmt->cas_init(caid, pmt_psi_buffer)
 *
 * Processing:
 * 1) decrypt:  on_em
 * 2) decrypt:  cas_check_em(em_packet_t)
 * 3) decrypt:  cam_send(em_packet_t)
 * 4) cam:      on_response->on_key
 * 5) decrypt:  cas_check_key
 */

/*
 * oooooooooo   o       oooooooo8 oooo   oooo ooooooooooo ooooooooooo
 *  888    888 888    o888     88  888  o88    888    88  88  888  88
 *  888oooo88 8  88   888          888888      888ooo8        888
 *  888      8oooo88  888o     oo  888  88o    888    oo      888
 * o888o   o88o  o888o 888oooo88  o888o o888o o888ooo8888    o888o
 *
 */

typedef struct em_packet_t em_packet_t;
struct em_packet_t
{
    uint8_t buffer[EM_MAX_SIZE];
    uint16_t buffer_size;

    uint16_t pnr;

    void (*on_key)(module_data_t *mod, em_packet_t *packet);
    module_data_t *mod;
};

/*
 *   oooooooo8     o      oooo     oooo
 * o888     88    888      8888o   888
 * 888           8  88     88 888o8 88
 * 888o     oo  8oooo88    88  888  88
 *  888oooo88 o88o  o888o o88o  8  o88o
 *
 */

typedef struct
{
    int is_ready;

    uint16_t caid;
    uint8_t ua[8];
    int disable_emm;

    asc_list_t *prov_list;
    asc_list_t *decrypt_list;

    module_data_t *self;
} module_cam_t;

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

typedef struct cas_t cas_t;

#define CAS_DATA()                                                                              \
    struct                                                                                      \
    {                                                                                           \
        int (*check_em)(cas_t *cas, em_packet_t *packet);                                       \
        int (*check_keys)(cas_t *cas, em_packet_t *packet);                                     \
        void (*destroy)(cas_t *cas);                                                            \
    } __cas

#define cas_data_set(_cas, _check_em, _check_keys, _destroy)                                    \
    _cas->__cas.check_em = _check_em;                                                           \
    _cas->__cas.check_keys = _check_keys;                                                       \
    _cas->__cas.destroy = _destroy

cas_t * cas_init(uint16_t caid, uint16_t pnr);
void cas_destroy(cas_t *cas);
int cas_check_em(cas_t *cas, em_packet_t *packet);
int cas_check_keys(cas_t *cas, em_packet_t *packet);

/*
 * ooooooooo  ooooooooooo  oooooooo8 oooooooooo ooooo  oooo oooooooooo  ooooooooooo
 *  888    88o 888    88 o888     88  888    888  888  88    888    888 88  888  88
 *  888    888 888ooo8   888          888oooo88     888      888oooo88      888
 *  888    888 888    oo 888o     oo  888  88o      888      888            888
 * o888ooo88  o888ooo8888 888oooo88  o888o  88o8   o888o    o888o          o888o
 *
 */

typedef struct
{
    module_cam_t *cam;
    cas_t *cas;

    void (*on_cam_ready)(module_data_t *mod);
    void (*on_cam_error)(module_data_t *mod);

    module_data_t *self;
} module_decrypt_t;

#define MODULE_DECRYPT_DATA() module_decrypt_t __decrypt

#endif /* _MODULE_CAM_H_ */
