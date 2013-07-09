/*
 * Astra DVB Module
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#include "../dvb.h"
#include <fcntl.h>

/* en50221: A.4.1.13 List of transport tags */
enum
{
    TT_SB                           = 0x80,
    TT_RCV                          = 0x81,
    TT_CREATE_TC                    = 0x82,
    TT_CTC_REPLY                    = 0x83,
    TT_DELETE_TC                    = 0x84,
    TT_DTC_REPLY                    = 0x85,
    TT_REQUEST_TC                   = 0x86,
    TT_NEW_TC                       = 0x87,
    TT_TC_ERROR                     = 0x88,
    TT_DATA_LAST                    = 0xA0,
    TT_DATA_MORE                    = 0xA1
};

/* en50221: 7.2.7 Coding of the session tags */
enum
{
    ST_SESSION_NUMBER               = 0x90,
    ST_OPEN_SESSION_REQUEST         = 0x91,
    ST_OPEN_SESSION_RESPONSE        = 0x92,
    ST_CREATE_SESSION               = 0x93,
    ST_CREATE_SESSION_RESPONSE      = 0x94,
    ST_CLOSE_SESSION_REQUEST        = 0x95,
    ST_CLOSE_SESSION_RESPONSE       = 0x96
};

/* en50221: Table 7: Open Session Status values */
enum
{
    SPDU_STATUS_OPENED              = 0x00,
    SPDU_STATUS_NOT_EXISTS          = 0xF0
};

/* en50221: 8.8.1 Resource Identifiers */
enum
{
    RI_RESOURCE_MANAGER             = 0x00010041,
    RI_APPLICATION_INFORMATION      = 0x00020041,
    RI_CONDITIONAL_ACCESS_SUPPORT   = 0x00030041,
    RI_HOST_CONTROL                 = 0x00200041,
    RI_DATE_TIME                    = 0x00240041,
    RI_MMI                          = 0x00400041
};

#define SIZE_INDICATOR 0x80

static uint8_t set_message_size(uint8_t *data, uint16_t size)
{
    if(size <= 0x7F)
    {
        data[0] = size;
        return 1;
    }
    else if(size < 0xFF)
    {
        data[0] = SIZE_INDICATOR | 1;
        data[1] = size;
        return 2;
    }
    else
    {
        data[0] = SIZE_INDICATOR | 2;
        data[1] = (size >> 8) & 0xFF;
        data[2] = (size     ) & 0xFF;
        return 3;
    }
}

static uint8_t get_message_size(const uint8_t *data, uint16_t *size)
{
    switch(data[0] & ~SIZE_INDICATOR)
    {
        case 0:
            *size = data[0];
            return 1;
        case 1:
            *size = data[1];
            return 2;
        case 2:
            *size = (data[1] << 8) | (data[2]);
            return 3;
        default:
            *size = 0;
            return 1;
    }
}

/*
 *  oooooooo8 oooooooooo ooooooooo  ooooo  oooo
 * 888         888    888 888    88o 888    88
 *  888oooooo  888oooo88  888    888 888    88
 *         888 888        888    888 888    88
 * o88oooo888 o888o      o888ooo88    888oo88
 *
 */
static void ca_slot_tpdu_send(module_data_t *mod, uint8_t slot_id
                              , uint8_t tag, const uint8_t *data, uint16_t size);

static void ca_spdu_open(module_data_t *mod, uint8_t slot_id)
{
    ca_slot_t *slot = &mod->slots[slot_id];

    uint16_t session_id = 0;
    ca_session_t *session = NULL;
    for(int i = 0; i < MAX_SESSIONS; ++i)
    {
        if(slot->sessions[i].resource_id == 0)
        {
            session_id = i + 1;
            session = &slot->sessions[i];
            break;
        }
    }
    if(!session_id)
    {
        asc_log_error(MSG("CA: session limit"));
        return;
    }

    const uint32_t resource_id = (slot->buffer[2] << 24)
                               | (slot->buffer[3] << 16)
                               | (slot->buffer[4] <<  8)
                               | (slot->buffer[5]      );

    session->resource_id = resource_id;

    uint8_t response[16];
    response[0] = ST_OPEN_SESSION_RESPONSE;
    response[1] = 0x07;
    if(   resource_id == RI_RESOURCE_MANAGER
       || resource_id == RI_APPLICATION_INFORMATION
       || resource_id == RI_CONDITIONAL_ACCESS_SUPPORT
       || resource_id == RI_DATE_TIME
       || resource_id == RI_MMI)
    {
        response[2] = SPDU_STATUS_OPENED;
    }
    else
    {
        response[2] = SPDU_STATUS_NOT_EXISTS;
    }
    memcpy(&response[3], &slot->buffer[2], 4);
    response[7] = session_id >> 8;
    response[8] = session_id & 0xFF;

    ca_slot_tpdu_send(mod, slot_id, TT_DATA_LAST, response, 9);
}

static void ca_spdu_close(module_data_t *mod, uint8_t slot_id)
{
    ca_slot_t *slot = &mod->slots[slot_id];
    const uint16_t session_id = (slot->buffer[2] << 8) | slot->buffer[3];

    // TODO: close callback
    slot->sessions[session_id].resource_id = 0;

    uint8_t response[8];
    response[0] = ST_CLOSE_SESSION_RESPONSE;
    response[1] = 0x03;
    response[2] = SPDU_STATUS_OPENED;
    response[3] = session_id >> 8;
    response[4] = session_id & 0xFF;

    ca_slot_tpdu_send(mod, slot_id, TT_DATA_LAST, response, 5);
}

static void ca_spdu_response_open(module_data_t *mod, uint8_t slot_id)
{
    ca_slot_t *slot = &mod->slots[slot_id];

    const uint16_t session_id = (slot->buffer[7] << 8) | slot->buffer[8];
    ca_session_t *session = &slot->sessions[session_id];

    const uint8_t status = slot->buffer[2];
    if(status != SPDU_STATUS_OPENED)
    {
        asc_log_error(MSG("CA: Slot %d failed to open session %d"), slot_id, session_id);
        session->resource_id = 0;
        return;
    }

    const uint32_t resource_id = (slot->buffer[2] << 24)
                               | (slot->buffer[3] << 16)
                               | (slot->buffer[4] <<  8)
                               | (slot->buffer[5]      );

    switch(resource_id)
    {
        case RI_RESOURCE_MANAGER:
            // TODO: open resource manager
            break;
        case RI_APPLICATION_INFORMATION:
            // TODO: open application information
            break;
        case RI_CONDITIONAL_ACCESS_SUPPORT:
            // TODO: open CA support
            break;
        case RI_DATE_TIME:
            // TODO: open data-time
            break;
        case RI_MMI:
            // TODO: open MMI
            break;
        case RI_HOST_CONTROL:
        default:
            asc_log_error(MSG("CA: Slot %d session %d unknown resource %d")
                          , slot_id, session_id, resource_id);
            session->resource_id = 0;
            break;
    }
}

static void ca_spdu_event(module_data_t *mod, uint8_t slot_id)
{
    ca_slot_t *slot = &mod->slots[slot_id];

    switch(slot->buffer[0])
    {
        case ST_SESSION_NUMBER:
        {
            if(slot->buffer_size <= 4)
                break;
            // const uint16_t session_id = (slot->buffer[2] << 8) | slot->buffer[3];
            // TODO: event callback (slot->sessions[session_id].event)
            break;
        }
        case ST_OPEN_SESSION_REQUEST:
        {
            if(slot->buffer_size != 6 || slot->buffer[1] != 0x04)
                break;
            ca_spdu_open(mod, slot_id);
            break;
        }
        case ST_CLOSE_SESSION_REQUEST:
        {
            if(slot->buffer_size != 4 || slot->buffer[1] != 0x02)
                break;
            ca_spdu_close(mod, slot_id);
            break;
        }
        case ST_CREATE_SESSION_RESPONSE:
        {
            if(slot->buffer_size != 9 || slot->buffer[1] != 0x07)
                break;
            ca_spdu_response_open(mod, slot_id);
            break;
        }
        case ST_CLOSE_SESSION_RESPONSE:
        {
            if(slot->buffer_size != 5 || slot->buffer[1] != 0x03)
                break;
            const uint16_t session_id = (slot->buffer[3] << 8) | slot->buffer[4];
            // TODO: close callback
            slot->sessions[session_id].resource_id = 0;
            break;
        }
        default:
            asc_log_error(MSG("CA: wrong SPDU tag 0x%02X"), slot->buffer[0]);
            break;
    }
}

/*
 * ooooooooooo oooooooooo ooooooooo  ooooo  oooo
 * 88  888  88  888    888 888    88o 888    88
 *     888      888oooo88  888    888 888    88
 *     888      888        888    888 888    88
 *    o888o    o888o      o888ooo88    888oo88
 *
 */

static void ca_tpdu_event(module_data_t *mod)
{
    int buffer_size = read(mod->ca_fd, mod->ca_buffer, MAX_TPDU_SIZE);
    if(buffer_size < 5)
    {
        if(buffer_size == -1)
            asc_log_error(MSG("CA: read failed. [%s]"), strerror(errno));
        else
            asc_log_error(MSG("CA: read failed. size:%d"), buffer_size);

        return;
    }

    const uint8_t slot_id = mod->ca_buffer[1] - 1;
    asc_log_debug(MSG("CA: Slot %d"), slot_id);
    const uint8_t tag = mod->ca_buffer[2];

    if(slot_id >= mod->slots_num)
    {
        asc_log_error(MSG("CA: read failed. wrong slot id %d"), slot_id);
        return;
    }

    ca_slot_t *slot = &mod->slots[slot_id];
    slot->is_busy = false;

    switch(tag)
    {
        case TT_CTC_REPLY:
        {
            slot->is_active = true;
            asc_log_info(MSG("CA: Slot %d is active"), slot_id);
            break;
        }
        case TT_DATA_LAST:
        case TT_DATA_MORE:
        {
            uint16_t buffer_skip = 3;
            uint16_t message_size = 0;
            buffer_skip += get_message_size(&mod->ca_buffer[3], &message_size);
            if(message_size <= 1)
                break;

            ++buffer_skip;
            --message_size;

            memcpy(&slot->buffer[slot->buffer_size]
                   , &mod->ca_buffer[buffer_skip]
                   , message_size);
            slot->buffer_size += message_size;

            if(tag == TT_DATA_LAST)
            {
                ca_spdu_event(mod, slot_id);
                slot->buffer_size = 0;
            }
            break;
        }
        case TT_SB:
            break;
        default:
            asc_log_warning(MSG("CA: Slot %d wrong tag 0x%02X"), slot_id, tag);
            break;
    }
}

static void ca_slot_tpdu_write(module_data_t *mod, uint8_t slot_id)
{
    ca_slot_t *slot = &mod->slots[slot_id];

    if(slot->is_busy)
    {
        asc_log_warning(MSG("CA: Slot %d is busy"), slot_id);
        return;
    }

    if(!asc_list_size(slot->queue))
    {
        asc_log_error(MSG("CA: Slot %d queue is empty"), slot_id);
        return;
    }

    asc_list_first(slot->queue);
    ca_tpdu_message_t *message = asc_list_data(slot->queue);
    asc_list_remove_current(slot->queue);

    if(write(mod->ca_fd, message->buffer, message->buffer_size) != message->buffer_size)
        asc_log_error(MSG("CA: Slot %d write failed"), slot_id);
    else
        slot->is_busy = true;

    free(message);
}

static void ca_slot_tpdu_send(module_data_t *mod, uint8_t slot_id
                              , uint8_t tag, const uint8_t *data, uint16_t size)
{
    ca_slot_t *slot = &mod->slots[slot_id];

    ca_tpdu_message_t *m = malloc(sizeof(ca_tpdu_message_t));
    uint8_t *buffer = m->buffer;
    uint16_t buffer_size = 3;

    buffer[0] = slot_id;
    buffer[1] = slot_id + 1;
    buffer[2] = tag;

    switch(tag)
    {
        case TT_RCV:
        case TT_CREATE_TC:
        case TT_CTC_REPLY:
        case TT_DELETE_TC:
        case TT_DTC_REPLY:
        case TT_REQUEST_TC:
        {
            buffer[3] = 1;
            buffer[4] = buffer[1];

            buffer_size += 2;
            break;
        }
        case TT_NEW_TC:
        case TT_TC_ERROR:
        {
            buffer[3] = 2;
            buffer[4] = buffer[1];
            buffer[5] = data[0];

            buffer_size += 3;
            break;
        }
        case TT_DATA_LAST:
        case TT_DATA_MORE:
        {
            buffer_size += set_message_size(&buffer[buffer_size], size + 1);

            buffer[buffer_size] = buffer[1];
            ++buffer_size;

            if(size > 0)
            {
                memcpy(&buffer[buffer_size], data, size);
                buffer_size += size;
            }

            break;
        }
        default:
            break;
    }

    m->buffer_size = buffer_size;
    asc_list_insert_tail(slot->queue, m);

    if(!slot->is_busy && asc_list_size(slot->queue) == 1)
        ca_slot_tpdu_write(mod, slot_id);
}

/*
 *  oooooooo8 ooooo         ooooooo   ooooooooooo
 * 888         888        o888   888o 88  888  88
 *  888oooooo  888        888     888     888
 *         888 888      o 888o   o888     888
 * o88oooo888 o888ooooo88   88ooo88      o888o
 *
 */

static void ca_slot_reset(module_data_t *mod, uint8_t slot_id)
{
    ca_slot_t *slot = &mod->slots[slot_id];

    if(ioctl(mod->ca_fd, CA_RESET, 1 << slot_id) != 0)
    {
        asc_log_error(MSG("CA: Slot %d CA_RESET failed"));
        return;
    }

    slot->is_active = false;
    slot->is_busy = false;

    for(asc_list_first(slot->queue); !asc_list_eol(slot->queue); asc_list_first(slot->queue))
    {
        free(asc_list_data(slot->queue));
        asc_list_remove_current(slot->queue);
    }

    for(int i = 0; i < MAX_SESSIONS; ++i)
    {
        ca_session_t *session = &slot->sessions[i];
        if(session->resource_id)
        {
            // TODO: close callback
            session->resource_id = 0;
        }
    }
}

static void ca_slot_loop(module_data_t *mod)
{
    for(int i = 0; i < mod->slots_num; ++i)
    {
        ca_slot_t *slot = &mod->slots[i];

        ca_slot_info_t slot_info;
        slot_info.num = i;
        if(ioctl(mod->ca_fd, CA_GET_SLOT_INFO, &slot_info) != 0)
        {
            asc_log_error(MSG("CA: Slot %d CA_GET_SLOT_INFO failed"), i);
            continue;
        }

        if(!(slot_info.flags & CA_CI_MODULE_READY))
        {
            if(slot->is_active)
            {
                asc_log_warning(MSG("CA: Slot %d is not ready"), i);
                ca_slot_reset(mod, i);
            }
            continue;
        }
        else if(!slot->is_active)
        {
            if(slot->is_busy)
            {
                asc_log_warning(MSG("CA: Slot %d resetting. timeout"), i);
                ca_slot_reset(mod, i);
            }
            else
            {
                // Slot initializing
                ca_slot_tpdu_send(mod, i, TT_CREATE_TC, NULL, 0);
            }
        }
    }

    // TODO: continue here...
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
            asc_log_error(MSG("CA: failed to open ca [%s]"), strerror(errno));
        mod->ca_fd = 0;
        return;
    }

    ca_caps_t caps;
    memset(&caps, 0, sizeof(ca_caps_t));
    if(ioctl(mod->ca_fd, CA_GET_CAP, &caps) != 0)
    {
        asc_log_error(MSG("CA: CA_GET_CAP failed [%s]"), strerror(errno));
        ca_close(mod);
        return;
    }

    asc_log_info(MSG("CA: Slots:%d"), caps.slot_num);
    if(!caps.slot_num)
    {
        ca_close(mod);
        return;
    }
    mod->slots_num = caps.slot_num;

    if(caps.slot_type & CA_CI)
        asc_log_info(MSG("CA:   CI high level interface"));
    if(caps.slot_type & CA_CI_LINK)
        asc_log_info(MSG("CA:   CI link layer level interface"));
    if(caps.slot_type & CA_CI_PHYS)
        asc_log_info(MSG("CA:   CI physical layer level interface"));
    if(caps.slot_type & CA_DESCR)
        asc_log_info(MSG("CA:   built-in descrambler"));
    if(caps.slot_type & CA_SC)
        asc_log_info(MSG("CA:   simple smart card interface"));

    asc_log_info(MSG("CA: Descramblers:%d"), caps.descr_num);

    if(caps.descr_type & CA_ECD)
        asc_log_info(MSG("CA:   ECD scrambling system"));
    if(caps.descr_type & CA_NDS)
        asc_log_info(MSG("CA:   NDS scrambling system"));
    if(caps.descr_type & CA_DSS)
        asc_log_info(MSG("CA:   DSS scrambling system"));

    if(!(caps.slot_type & CA_CI_LINK))
    {
        asc_log_error(MSG("CI link layer level interface is not supported"));
        ca_close(mod);
        return;
    }

    mod->slots = calloc(caps.slot_num, sizeof(ca_slot_t));

    for(int i = 0; i < mod->slots_num; ++i)
    {
        ca_slot_t *slot = &mod->slots[i];
        slot->queue = asc_list_init();

        ca_slot_info_t slot_info;
        slot_info.num = i;
        if(ioctl(mod->ca_fd, CA_GET_SLOT_INFO, &slot_info) != 0)
        {
            asc_log_error(MSG("CA: Slot %d CA_GET_SLOT_INFO failed"), i);
            continue;
        }

        if(slot_info.flags & CA_CI_MODULE_READY)
            asc_log_info(MSG("CA: Slot %d ready to go"), i);
        else
            asc_log_info(MSG("CA: Slot %d is not ready"), i);
    }
}

void ca_close(module_data_t *mod)
{
    if(mod->ca_fd > 0)
    {
        close(mod->ca_fd);
        mod->ca_fd = 0;

        for(int i = 0; i < mod->slots_num; ++i)
        {
            ca_slot_t *slot = &mod->slots[i];
            for(asc_list_first(slot->queue)
                ; !asc_list_eol(slot->queue)
                ; asc_list_first(slot->queue))
            {
                free(asc_list_data(slot->queue));
                asc_list_remove_current(slot->queue);
            }
            asc_list_destroy(slot->queue);
        }

        free(mod->slots);
        mod->slots = NULL;
    }
}

void ca_loop(module_data_t *mod, int is_data)
{
    if(is_data)
    {
        ca_tpdu_event(mod);
        return;
    }

    ca_slot_loop(mod);
}
