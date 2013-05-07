/*
 * Astra DVB Module
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#include "../dvb.h"
#include <fcntl.h>

#define MAX_TPDU_SIZE 2048

enum
{
    TPDU_SB         = 0x80,
    TPDU_RCV        = 0x81,
    TPDU_CREATE_TC  = 0x82,
    TPDU_CTC_REPLY  = 0x83,
    TPDU_DELETE_TC  = 0x84,
    TPDU_DTC_REPLY  = 0x85,
    TPDU_REQUEST_TC = 0x86,
    TPDU_NEW_TC     = 0x87,
    TPDU_TC_ERROR   = 0x88,
    TPDU_DATA_LAST  = 0xA0,
    TPDU_DATA_MORE  = 0xA1,
};

#define MKRID(CLASS, TYPE, VERSION)                                     \
    ((((CLASS)&0xffff)<<16) | (((TYPE)&0x3ff)<<6) | ((VERSION)&0x3f))

#define EN50221_APP_AI_RESOURCEID MKRID(2,1,1)

/*
 *      o       oooooooo8 oooo   oooo      oo
 *     888     888         8888o  88     o888
 *    8  88     888oooooo  88 888o88      888
 *   8oooo88           888 88   8888 ooo  888
 * o88o  o888o o88oooo888 o88o    88 888 o888o
 *
 */

static int asn_1_decode(uint16_t *length, const uint8_t *asn_1_array, uint32_t asn_1_array_len)
{
    uint8_t length_field;

    if(asn_1_array_len < 1)
        return 0;

    length_field = asn_1_array[0];

    if(length_field < 0x80)
    { // there is only one word
        *length = length_field & 0x7f;
        return 1;
    }
    else if(length_field == 0x81)
    {
        if(asn_1_array_len < 2)
            return 0;

        *length = asn_1_array[1];
        return 2;
    }
    else if(length_field == 0x82)
    {
        if(asn_1_array_len < 3)
            return 0;

        *length = (asn_1_array[1] << 8) | asn_1_array[2];
        return 3;
    }

    return 0;
}

int asn_1_encode(uint16_t length, uint8_t *asn_1_array, uint32_t asn_1_array_len)
{
    if(length < 0x80)
    {
        if(asn_1_array_len < 1)
            return 0;

        asn_1_array[0] = length & 0x7f;
        return 1;
    }
    else if(length < 0x100)
    {
        if(asn_1_array_len < 2)
            return -1;

        asn_1_array[0] = 0x81;
        asn_1_array[1] = length;
        return 2;
    }

    /* else */
    if (asn_1_array_len < 3)
        return -1;

    asn_1_array[0] = 0x82;
    asn_1_array[1] = length >> 8;
    asn_1_array[2] = length;
    return 3;
}

/*
 * ooooooooooo ooooo  oooo ooooooooooo oooo   oooo ooooooooooo
 *  888    88   888    88   888    88   8888o  88  88  888  88
 *  888ooo8      888  88    888ooo8     88 888o88      888
 *  888    oo     88888     888    oo   88   8888      888
 * o888ooo8888     888     o888ooo8888 o88o    88     o888o
 *
 */

static const char conn_is_not_active[] = "connection is not active";

static void ca_event(module_data_t *mod)
{
    uint8_t buffer[MAX_TPDU_SIZE];
    int buffer_size = read(mod->ca_fd, buffer, sizeof(buffer));
    if(buffer_size < 5)
    {
        asc_log_error(MSG("en50221: read() failed [%s]"), strerror(errno));
        return;
    }

    const uint8_t slot_id = buffer[0];
    asc_log_debug(MSG("en50221: slot_id:%d"), slot_id);

    const uint8_t *buffer_ptr = &buffer[2];
    buffer_size -= 2;

    uint16_t asn_data_length;
    int length_field_size;

    while(buffer_size > 0)
    {
        const uint8_t tag = buffer_ptr[2];
        length_field_size = asn_1_decode(&asn_data_length, &buffer_ptr[1], buffer_size - 1);
        if(length_field_size == 0)
        {
            asc_log_error(MSG("en50221: invalid asn. slot:%d\n"), slot_id);
            return;
        }
        if(asn_data_length == 0)
        {
            asc_log_error(MSG("en50221: invalid length. slot:%d"), slot_id);
            return;
        }

        buffer_ptr += 1 + length_field_size;
        const uint8_t conn_id = buffer_ptr[0];
        ca_connection_t *conn = &mod->slots[slot_id].connections[conn_id];
        buffer_ptr += 1;

        asc_log_debug(MSG("en50221: conn_id:%d tag:0x%02X"), conn_id, tag);

        --asn_data_length;

        switch(tag) /* en50221: A.4.1.13 List of transport tags */
        {
            case 0x83: /* c_t_c_reply */
            {
                if(conn->state == CA_CONN_CREATE)
                {
                    conn->state = CA_CONN_ACTIVE;
                    // callback [ connection_open ]
                }
                else
                {
                    asc_log_error(MSG("en50221: wrong connection state. slot:%d"), slot_id);
                    return;
                }
                break;
            }
            case 0x84: /* delete_t_c */
            {
                if(conn->state & (CA_CONN_ACTIVE | CA_CONN_DELETE))
                {
                    conn->state = CA_CONN_IDLE;
                    // free buffer

                    uint8_t hdr[5];
                    hdr[0] = slot_id;
                    hdr[1] = conn_id;
                    hdr[2] = 0x85; /* d_t_c_reply */
                    hdr[3] = 1;
                    hdr[4] = conn_id;

                    if(write(mod->ca_fd, hdr, sizeof(hdr)) != sizeof(hdr))
                    {
                        asc_log_error(MSG("en50221: failed to send d_t_c_reply. slot:%d")
                                      , slot_id);
                        return;
                    }

                    // callback [ connection_close ]
                }
                else
                {
                    asc_log_error(MSG("en50221: %s. slot:%d"), conn_is_not_active, slot_id);
                    return;
                }
                break;
            }
            case 0x85: /* d_t_c_reply */
            {
                if(conn->state == CA_CONN_DELETE)
                    conn->state = CA_CONN_IDLE;
                else
                {
                    asc_log_error(MSG("en50221: connection is not closed. slot:%d"), slot_id);
                    return;
                }
                break;
            }
            case 0x86: /* request_t_c */
            {
                int new_conn_id = -1;
                for(int i = 0; i < CA_MAX_CONNECTIONS; ++i)
                {
                    if(mod->slots[slot_id].connections[i].state == CA_CONN_IDLE)
                    {
                        new_conn_id = i;
                        break;
                    }
                }
                break;
            }
            case 0xA1: /* data_more */
            {
                if(conn->state != CA_CONN_ACTIVE)
                {
                    asc_log_error(MSG("en50221: %s. slot:%d"), conn_is_not_active, slot_id);
                    return;
                }
                break;
            }
            case 0xA0: /* data_last */
            {
                if(conn->state != CA_CONN_ACTIVE)
                {
                    asc_log_error(MSG("en50221: %s. slot:%d"), conn_is_not_active, slot_id);
                    return;
                }
                break;
            }
            case 0x80: /* SB */
            {
                if(conn->state != CA_CONN_ACTIVE)
                {
                    asc_log_error(MSG("en50221: %s. slot:%d"), conn_is_not_active, slot_id);
                    return;
                }
                break;
            }
            default:
                asc_log_error(MSG("en50221: unexpected TPDU tag 0x%02x. slot:%d"), tag, slot_id);
                return;
        }

        buffer_ptr += asn_data_length;
        buffer_size -= asn_data_length;
    }
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

    mod->slots_num = caps.slot_num;
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

    mod->slots = calloc(caps.slot_num, sizeof(ca_slot_t));
}

void ca_close(module_data_t *mod)
{
    if(mod->ca_fd > 0)
    {
        close(mod->ca_fd);
        mod->ca_fd = 0;
        free(mod->slots);
        mod->slots = NULL;
    }
}

void ca_loop(module_data_t *mod, int is_data)
{
    if(!is_data)
        ca_close(mod);
    else
        ca_event(mod);
}
