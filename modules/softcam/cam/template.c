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

#define LOG_MSG(_msg) "[%s %s] " _msg, mod->__cam_module.name, mod->config.name

struct module_data_s
{
    CAM_MODULE_BASE();

    struct
    {
        char *name;
        char *cas_data;
        // options
    } config;

    // instance variables
};

/* module code */

/* softcam callbacks */

static void interface_send_em(module_data_t *mod)
{
    cam_packet_t *packet = list_get_data(mod->__cam_module.queue.current);
}

/* required */

static void module_init(module_data_t *mod)
{
    log_debug(LOG_MSG("init"));

    if(mod->config.cas_data)
        cam_set_cas_data(mod, mod->config.cas_data);

    CAM_INTERFACE();
}

static void module_destroy(module_data_t *mod)
{
    log_debug(LOG_MSG("destroy"));

    cam_queue_flush(mod);
    decrypt_module_cam_status(mod, -1);
}

MODULE_OPTIONS()
{
    OPTION_STRING("name"    , config.name       , 1, NULL)
    OPTION_STRING("cas_data", config.cas_data   , 0, NULL)
};

MODULE_METHODS_EMPTY();

MODULE(template)
