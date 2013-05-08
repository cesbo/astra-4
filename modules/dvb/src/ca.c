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

/* en50221: 7.2.7 Coding of the session tags */
enum
{
    SPDU_SESSION_NUMBER     = 0x90,
    SPDU_OPEN_SESSION_REQ   = 0x91,
    SPDU_OPEN_SESSION_RES   = 0x92,
    SPDU_CREATE_SESSION     = 0x93,
    SPDU_CREATE_SESSION_RES = 0x94,
    SPDU_CLOSE_SESSION_REQ  = 0x95,
    SPDU_CLOSE_SESSION_RES  = 0x96
};

enum
{
    SPDU_STATUS_OPEN        = 0x00
};

typedef enum
{
    LLCI_CONNECT        = 0x01,
    LLCI_CAM_CONNECT    = 0x02,
    LLCI_CLOSE          = 0x03
} llci_event_t;

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
 * ooooo       ooooo         oooooooo8 ooooo
 *  888         888        o888     88  888
 *  888         888        888          888
 *  888      o  888      o 888o     oo  888
 * o888ooooo88 o888ooooo88  888oooo88  o888o
 *
 */

static int llci_lookup(module_data_t *mod, uint32_t resource_id, uint32_t *connected_resource_id)
{
    return 0;
}

static void llci_session(module_data_t *mod, uint32_t resource_id, llci_event_t event
                         , uint8_t slot_id, uint8_t conn_id)
{

}

/*
 *  oooooooo8 oooooooooo ooooooooo  ooooo  oooo
 * 888         888    888 888    88o 888    88
 *  888oooooo  888oooo88  888    888 888    88
 *         888 888        888    888 888    88
 * o88oooo888 o888o      o888ooo88    888oo88
 *
 */

static void ca_session_data(module_data_t *mod, uint8_t slot_id, uint8_t conn_id)
{
    ca_connection_t *conn = &mod->slots[slot_id].connections[conn_id];

    const uint8_t *buffer_ptr = conn->buffer;
    uint16_t buffer_size = conn->buffer_size;
    const uint8_t spdu_tag = buffer_ptr[0];
    ++buffer_ptr;
    --buffer_size;

    asc_log_debug(MSG("en50221: conn_id:%d spdu_tag:0x%02X"), conn_id, spdu_tag);

    switch(spdu_tag)
    {
        case SPDU_OPEN_SESSION_REQ:
        {
            if((buffer_size < 5) || (buffer_ptr[0] != 4))
            {
                asc_log_error(MSG("en50221: invalid SPDU length. slot:%d"), slot_id);
                return;
            }

            const uint32_t resource_id = (buffer_ptr[1] << 24)
                                       | (buffer_ptr[2] << 16)
                                       | (buffer_ptr[3] << 8)
                                       | (buffer_ptr[4]);

            uint32_t connected_resource_id;
            const int status = llci_lookup(mod, resource_id, &connected_resource_id);

            // TODO: allocate session
            // TODO: send response
            // TODO: callback

            break;
        }
        case SPDU_CLOSE_SESSION_REQ:
        {
            break;
        }
        case SPDU_SESSION_NUMBER:
        {
            break;
        }
        case SPDU_CREATE_SESSION_RES:
        {
            break;
        }
        case SPDU_CLOSE_SESSION_RES:
        {
            break;
        }
        default:
            asc_log_error(MSG("en50221: unexpected TPDU tag 0x%02x. slot:%d")
                          , spdu_tag, slot_id);
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

static void ca_transport_event(module_data_t *mod)
{
    static const char conn_is_not_active[] = "connection is not active";

    int buffer_size = read(mod->ca_fd, mod->ca_buffer, MAX_TPDU_SIZE);
    if(buffer_size < 5)
    {
        asc_log_error(MSG("en50221: read() failed [%s]"), strerror(errno));
        return;
    }

    const uint8_t slot_id = mod->ca_buffer[0];
    asc_log_debug(MSG("en50221: slot_id:%d"), slot_id);

    const uint8_t *buffer_ptr = &mod->ca_buffer[2];
    buffer_size -= 2;

    uint16_t asn_data_length;
    int length_field_size;
    uint8_t hdr[6];
    hdr[0] = slot_id;

    while(buffer_size > 0)
    {
        const uint8_t tpdu_tag = buffer_ptr[2];
        length_field_size = asn_1_decode(&asn_data_length, &buffer_ptr[1], buffer_size - 1);
        if(length_field_size == 0)
        {
            asc_log_error(MSG("en50221: invalid asn. slot:%d\n"), slot_id);
            return;
        }
        if(asn_data_length == 0)
        {
            asc_log_error(MSG("en50221: invalid TPDU length. slot:%d"), slot_id);
            return;
        }

        buffer_ptr += 1 + length_field_size;
        const uint8_t conn_id = buffer_ptr[0];
        ca_connection_t *conn = &mod->slots[slot_id].connections[conn_id];
        ++buffer_ptr;
        --asn_data_length;

        asc_log_debug(MSG("en50221: conn_id:%d tpdu_tag:0x%02X"), conn_id, tpdu_tag);

        switch(tpdu_tag)
        {
            case TPDU_CTC_REPLY:
            {
                if(conn->state == CA_CONN_CREATE)
                {
                    conn->state = CA_CONN_ACTIVE;
                    llci_session(mod, 0, LLCI_CONNECT, slot_id, conn_id);
                }
                else
                {
                    asc_log_error(MSG("en50221: wrong connection state. slot:%d"), slot_id);
                    return;
                }
                break;
            }
            case TPDU_DELETE_TC:
            {
                if(conn->state & (CA_CONN_ACTIVE | CA_CONN_DELETE))
                {
                    conn->state = CA_CONN_IDLE;
                    conn->buffer_size = 0;

                    hdr[1] = conn_id;
                    hdr[2] = TPDU_DTC_REPLY;
                    hdr[3] = 1;
                    hdr[4] = conn_id;

                    if(write(mod->ca_fd, hdr, 5) != 5)
                    {
                        asc_log_error(MSG("en50221: failed to send TPDU_DTC_REPLY. slot:%d")
                                      , slot_id);
                        return;
                    }

                    // TODO: get resource_id
                    uint32_t resource_id = 0;
                    llci_session(mod, resource_id, LLCI_CLOSE, slot_id, conn_id);
                }
                else
                {
                    asc_log_error(MSG("en50221: %s. slot:%d"), conn_is_not_active, slot_id);
                    return;
                }
                break;
            }
            case TPDU_DTC_REPLY:
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
            case TPDU_REQUEST_TC:
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

                hdr[1] = conn_id;
                hdr[3] = 2;
                hdr[4] = conn_id;

                if(new_conn_id == -1)
                {
                    asc_log_error(MSG("en50221: too many connections. slot:%d"), slot_id);

                    hdr[2] = TPDU_TC_ERROR;
                    hdr[5] = 1;
                    if(write(mod->ca_fd, hdr, 6) != 6)
                    {
                        asc_log_error(MSG("en50221: failed to send TPDU_TC_ERROR. slot:%d")
                                      , slot_id);
                        return;
                    }
                }
                else
                {
                    hdr[2] = TPDU_NEW_TC;
                    hdr[5] = new_conn_id;
                    if(write(mod->ca_fd, hdr, 6) != 6)
                    {
                        asc_log_error(MSG("en50221: failed to send TPDU_NEW_TC. slot:%d")
                                      , slot_id);
                        return;
                    }

                    hdr[1] = new_conn_id;
                    hdr[2] = TPDU_CREATE_TC;
                    hdr[3] = 1;
                    hdr[4] = new_conn_id;
                    if(write(mod->ca_fd, hdr, 5) != 5)
                    {
                        asc_log_error(MSG("en50221: failed to send TPDU_CREATE_TC. slot:%d")
                                      , slot_id);
                        return;
                    }

                    conn = &mod->slots[slot_id].connections[new_conn_id];
                    gettimeofday(&conn->tx_time, NULL);

                    llci_session(mod, 0, LLCI_CAM_CONNECT, slot_id, conn_id);
                }
                break;
            }
            case TPDU_DATA_MORE:
            case TPDU_DATA_LAST:
            {
                if(conn->state != CA_CONN_ACTIVE)
                {
                    asc_log_error(MSG("en50221: %s. slot:%d"), conn_is_not_active, slot_id);
                    return;
                }
                if(asn_data_length + conn->buffer_size > MAX_TPDU_SIZE)
                {
                    asc_log_error(MSG("en50221: TPDU buffer overflow. slot:%d"), slot_id);
                    return;
                }
                conn->tx_time.tv_sec = 0;
                memcpy(&conn->buffer[conn->buffer_size], buffer_ptr, asn_data_length);
                conn->buffer_size += asn_data_length;

                if(tpdu_tag == TPDU_DATA_LAST)
                {
                    ca_session_data(mod, slot_id, conn_id);
                    conn->buffer_size = 0;
                }

                break;
            }
            case TPDU_SB:
            {
                if(conn->state != CA_CONN_ACTIVE)
                {
                    asc_log_error(MSG("en50221: %s. slot:%d"), conn_is_not_active, slot_id);
                    return;
                }
                if(asn_data_length != 1)
                {
                    asc_log_error(MSG("en50221: SB invalid length. slot:%d"), slot_id);
                    return;
                }
                if(buffer_ptr[0] & 0x80)
                {
                    hdr[1] = conn_id;
                    hdr[2] = TPDU_RCV;
                    hdr[3] = 1;
                    hdr[4] = conn_id;
                    if(write(mod->ca_fd, hdr, 5) != 5)
                    {
                        asc_log_error(MSG("en50221: failed to send TPDU_RCV. slot:%d"), slot_id);
                        return;
                    }

                    gettimeofday(&conn->tx_time, NULL);
                }
                else
                    conn->tx_time.tv_sec = 0;

                break;
            }
            default:
                asc_log_error(MSG("en50221: unexpected TPDU tag 0x%02x. slot:%d")
                              , tpdu_tag, slot_id);
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
        asc_log_error(MSG("CI link layer level interface is not supported"));
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
        ca_transport_event(mod);
}
