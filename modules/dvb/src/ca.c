/*
 * Astra DVB Module
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#include "../dvb.h"
#include <fcntl.h>

#define MAX_TPDU_SIZE  2048

static void ca_clear(module_data_t *mod, int slot)
{
    if(mod->ca_fd <= 0)
        return;

    if(ioctl(mod->ca_fd, CA_RESET, 1 << slot) != 0)
        asc_log_warning(MSG("CA_RESET failed. slot:%d [%s]"), slot, strerror(errno));

    ca_slot_t *ca = &mod->ca[slot];
    ca->is_active = 0;
}

/*
 * ooooooooooo ooooo  oooo ooooooooooo oooo   oooo ooooooooooo
 *  888    88   888    88   888    88   8888o  88  88  888  88
 *  888ooo8      888  88    888ooo8     88 888o88      888
 *  888    oo     88888     888    oo   88   8888      888
 * o888ooo8888     888     o888ooo8888 o88o    88     o888o
 *
 */

static void ca_event(module_data_t *mod)
{
    uint8_t buffer[MAX_TPDU_SIZE];
    int buffer_size = read(mod->ca_fd, buffer, sizeof(buffer));
    if(buffer_size < 5)
    {
        asc_log_error(MSG("en50221: read() failed [%s]"), strerror(errno));
        return;
    }

    const uint8_t slot = buffer[0];
    const uint8_t tcid = buffer[1];
    const uint8_t tag = buffer[2];
    asc_log_debug(MSG("en50221: slot:%d tcid:%d tag:0x%02X"), slot, tcid, tag);
}

/*
 * oooooooooo      o       oooooooo8 ooooooooooo
 *  888    888    888     888         888    88
 *  888oooo88    8  88     888oooooo  888ooo8
 *  888    888  8oooo88           888 888    oo
 * o888ooo888 o88o  o888o o88oooo888 o888ooo8888
 *
 */

void ca_open(module_data_t *mod)
{
    char dev_name[32];
    sprintf(dev_name, "/dev/dvb/adapter%d/ca%d", mod->adapter, mod->device);

    mod->ca_fd = open(dev_name, O_RDWR | O_NONBLOCK);
    if(mod->ca_fd <= 0)
    {
        if(errno != ENOENT)
            asc_log_error(MSG("failed to open ca [%s]"), strerror(errno));
        mod->ca_fd = 0;
        return;
    }

    ca_caps_t caps;
    memset(&caps, 0, sizeof(ca_caps_t));
    if(ioctl(mod->ca_fd, CA_GET_CAP, &caps) != 0 )
    {
        asc_log_error(MSG("CA_GET_CAP failed [%s]"), strerror(errno));
        ca_close(mod);
        return;
    }

    if(!caps.slot_num)
    {
        asc_log_error(MSG("CA with no slots"));
        ca_close(mod);
        return;
    }

    asc_log_debug(MSG("CA slots: %d"), caps.slot_num);

    if(caps.slot_type & CA_CI)
        asc_log_debug(MSG("CI high level interface"));
    if(caps.slot_type & CA_CI_LINK)
        asc_log_debug(MSG("CI link layer level interface"));
    if(caps.slot_type & CA_CI_PHYS)
        asc_log_debug(MSG("CI physical layer level interface"));
    if(caps.slot_type & CA_DESCR)
        asc_log_debug(MSG("built-in descrambler"));
    if(caps.slot_type & CA_SC)
        asc_log_debug(MSG("simple smart card interface"));

    asc_log_debug(MSG("CA descramblers: %d"), caps.descr_num);

    if(caps.descr_type & CA_ECD)
        asc_log_debug(MSG("ECD scrambling system"));
    if(caps.descr_type & CA_NDS)
        asc_log_debug(MSG("NDS scrambling system"));
    if(caps.descr_type & CA_DSS)
        asc_log_debug(MSG("DSS scrambling system"));

    if(!(caps.slot_type & CA_CI_LINK))
    {
        asc_log_error(MSG("CI link layer interface is not supported"));
        ca_close(mod);
        return;
    }

    mod->ca = calloc(caps.slot_num, sizeof(ca_slot_t));
    for(uint32_t i = 0; i < caps.slot_num; ++i)
        ca_clear(mod, i);
}

void ca_close(module_data_t *mod)
{
    if(mod->ca_fd > 0)
    {
        close(mod->ca_fd);
        mod->ca_fd = 0;
        free(mod->ca);
        mod->ca = NULL;
    }
}

void ca_loop(module_data_t *mod, int is_data)
{
    if(is_data)
    {
        ca_event(mod);
        return;
    }
}
