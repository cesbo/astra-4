/*
 * Astra SoftCAM module
 * Copyright (C) <year>, <name> <email>
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

#define LOG_MSG(_msg) "[%s %s] " _msg, mod->__name, mod->__cam_module.name

struct module_data_s
{
    CAM_MODULE_BASE();

    struct
    {
        // options
    } config;

    // instance variables
};

/* module code */

/* softcam callbacks */

static void interface_send_em(module_data_t *mod)
{
    cam_packet_t *packet = list_get_data(mod->__cam_module.queue.head);
}

static void interface_activate(module_data_t *mod, int is_active)
{
    ;
}

/* required */

static void module_configure(module_data_t *mod)
{
    module_set_string(mod, "name", 1, NULL, &mod->__cam_module.name);

    const char *cas_data = NULL;
    if(module_set_string(mod, "cas_data", 0, NULL, &cas_data))
        cam_set_cas_data(mod, cas_data);
}

static void module_initialize(module_data_t *mod)
{
    module_configure(mod);

    CAM_INTERFACE();
}

static void module_destroy(module_data_t *mod)
{
    cam_queue_flush(mod, NULL);
    decrypt_module_cam_status(mod, -1);
}

MODULE_METHODS_EMPTY();

MODULE(template)
