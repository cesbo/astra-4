/*
 * Astra Module: SoftCAM
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
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
#   include "../module_cam.h"
#endif

typedef module_cas_t * (*cas_init_t)(module_decrypt_t *decrypt);

module_cas_t * bulcrypt_cas_init(module_decrypt_t *decrypt);
module_cas_t * conax_cas_init(module_decrypt_t *decrypt);
module_cas_t * cryptoworks_cas_init(module_decrypt_t *decrypt);
module_cas_t * dgcrypt_cas_init(module_decrypt_t *decrypt);
module_cas_t * dre_cas_init(module_decrypt_t *decrypt);
module_cas_t * exset_cas_init(module_decrypt_t *decrypt);
module_cas_t * griffin_cas_init(module_decrypt_t *decrypt);
module_cas_t * irdeto_cas_init(module_decrypt_t *decrypt);
module_cas_t * mediaguard_cas_init(module_decrypt_t *decrypt);
module_cas_t * nagra_cas_init(module_decrypt_t *decrypt);
module_cas_t * videoguard_cas_init(module_decrypt_t *decrypt);
module_cas_t * viaccess_cas_init(module_decrypt_t *decrypt);

cas_init_t cas_init_list[] =
{
    bulcrypt_cas_init,
    conax_cas_init,
    cryptoworks_cas_init,
    dgcrypt_cas_init,
    dre_cas_init,
    exset_cas_init,
    griffin_cas_init,
    irdeto_cas_init,
    mediaguard_cas_init,
    nagra_cas_init,
    videoguard_cas_init,
    viaccess_cas_init,
    NULL
};
