
#ifndef _MODULE_CAM_H_
#   include "../module_cam.h"
#endif

typedef module_cas_t * (*cas_init_t)(module_decrypt_t *decrypt);

module_cas_t * irdeto_cas_init(module_decrypt_t *decrypt);
module_cas_t * viaccess_cas_init(module_decrypt_t *decrypt);
module_cas_t * dre_cas_init(module_decrypt_t *decrypt);
module_cas_t * conax_cas_init(module_decrypt_t *decrypt);
module_cas_t * nagra_cas_init(module_decrypt_t *decrypt);

cas_init_t cas_init_list[] =
{
    irdeto_cas_init,
    viaccess_cas_init,
    dre_cas_init,
    nagra_cas_init,
    NULL
};
