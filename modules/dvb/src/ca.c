/*
 * Astra DVB Module
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#include "../dvb.h"
#include <fcntl.h>
#include <netinet/in.h>

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

/* en50221: Table 58: Application object tag values */
enum
{
    AOT_PROFILE_ENQ                 = 0x9F8010,
    AOT_PROFILE                     = 0x9F8011,
    AOT_PROFILE_CHANGE              = 0x9F8012,
    AOT_APPLICATION_INFO_ENQ        = 0x9F8020,
    AOT_APPLICATION_INFO            = 0x9F8021,
    AOT_ENTER_MENU                  = 0x9F8022,
    AOT_CA_INFO_ENQ                 = 0x9F8030,
    AOT_CA_INFO                     = 0x9F8031,
    AOT_CA_PMT                      = 0x9F8032,
    AOT_CA_PMT_REPLY                = 0x9F8033,
    AOT_CA_UPDATE                   = 0x9F8034,
    AOT_TUNE                        = 0x9F8400,
    AOT_REPLACE                     = 0x9F8401,
    AOT_CLEAR_REPLACE               = 0x9F8402,
    AOT_ASK_RELEASE                 = 0x9F8403,
    AOT_DATE_TIME_ENQ               = 0x9F8440,
    AOT_DATE_TIME                   = 0x9F8441,
    AOT_CLOSE_MMI                   = 0x9F8800,
    AOT_DISPLAY_CONTROL             = 0x9F8801,
    AOT_DISPLAY_REPLY               = 0x9F8802,
    AOT_TEXT_LAST                   = 0x9F8803,
    AOT_TEXT_MORE                   = 0x9F8804,
    AOT_KEYPAD_CONTROL              = 0x9F8805,
    AOT_KEYPRESS                    = 0x9F8806,
    AOT_ENQ                         = 0x9F8807,
    AOT_ANSW                        = 0x9F8808,
    AOT_MENU_LAST                   = 0x9F8809,
    AOT_MENU_MORE                   = 0x9F880A,
    AOT_MENU_ANSW                   = 0x9F880B,
    AOT_LIST_LAST                   = 0x9F880C,
    AOT_LIST_MORE                   = 0x9F880D,
    AOT_SUBTITLE_SEGMENT_LAST       = 0x9F880E,
    AOT_SUBTITLE_SEGMENT_MORE       = 0x9F880F,
    AOT_DISPLAY_MESSAGE             = 0x9F8810,
    AOT_SCENE_END_MARK              = 0x9F8811,
    AOT_SCENE_DONE                  = 0x9F8812,
    AOT_SCENE_CONTROL               = 0x9F8813,
    AOT_SUBTITLE_DOWNLOAD_LAST      = 0x9F8814,
    AOT_SUBTITLE_DOWNLOAD_MORE      = 0x9F8815,
    AOT_FLUSH_DOWNLOAD              = 0x9F8816,
    AOT_DOWNLOAD_REPLY              = 0x9F8817,
    AOT_COMMS_CMD                   = 0x9F8C00,
    AOT_CONNECTION_DESCRIPTOR       = 0x9F8C01,
    AOT_COMMS_REPLY                 = 0x9F8C02,
    AOT_COMMS_SEND_LAST             = 0x9F8C03,
    AOT_COMMS_SEND_MORE             = 0x9F8C04,
    AOT_COMMS_RCV_LAST              = 0x9F8C05,
    AOT_COMMS_RCV_MORE              = 0x9F8C06
};

#define SPDU_HEADER_SIZE 4
#define APDU_TAG_SIZE 3

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

static void ca_tpdu_send(module_data_t *mod, uint8_t slot_id
                              , uint8_t tag, const uint8_t *data, uint16_t size);

static void ca_apdu_send(module_data_t *mod, uint8_t slot_id, uint16_t session_id
                         , uint32_t tag, const uint8_t *data, uint16_t size);

static uint32_t ca_apdu_get_tag(module_data_t *mod, uint8_t slot_id);
static uint8_t * ca_apdu_get_buffer(module_data_t *mod, uint8_t slot_id, uint16_t *size);

/*
 * oooooooooo  ooooooooooo  oooooooo8
 *  888    888  888    88  888
 *  888oooo88   888ooo8     888oooooo
 *  888  88o    888    oo          888 ooo
 * o888o  88o8 o888ooo8888 o88oooo888  888
 *
 */

/*
 * Resource Manager
 */

static void resource_manager_event(module_data_t *mod, uint8_t slot_id, uint16_t session_id)
{
    const uint32_t tag = ca_apdu_get_tag(mod, slot_id);
    switch(tag)
    {
        case AOT_PROFILE_ENQ:
        {
            uint32_t res[] =
            {
                htonl(RI_RESOURCE_MANAGER),
                htonl(RI_APPLICATION_INFORMATION),
                htonl(RI_CONDITIONAL_ACCESS_SUPPORT),
                htonl(RI_DATE_TIME),
                htonl(RI_MMI)
            };
            ca_apdu_send(mod, slot_id, session_id, AOT_PROFILE, (const uint8_t *)res, sizeof(res));
            break;
        }
        case AOT_PROFILE:
        {
            ca_apdu_send(mod, slot_id, session_id, AOT_PROFILE_CHANGE, NULL, 0);
            break;
        }
        default:
            asc_log_error(MSG("CA: Resource Manager. Wrong event:0x%08X"), tag);
            break;
    }
}

static void resource_manager_open(module_data_t *mod, uint8_t slot_id, uint16_t session_id)
{
    asc_log_debug(MSG("CA: %s(): slot_id:%d session_id:%d"), __FUNCTION__, slot_id, session_id);

    ca_session_t *session = &mod->slots[slot_id].sessions[session_id];
    session->event = resource_manager_event;
    ca_apdu_send(mod, slot_id, session_id, AOT_PROFILE_ENQ, NULL, 0);
}

/*
 * Application Information
 */

static void application_information_event(module_data_t *mod, uint8_t slot_id, uint16_t session_id)
{
    __uarg(session_id);

    const uint32_t tag = ca_apdu_get_tag(mod, slot_id);
    switch(tag)
    {
        case AOT_APPLICATION_INFO:
        {
            uint16_t size = 0;
            const uint8_t *buffer = ca_apdu_get_buffer(mod, slot_id, &size);
            if(size < 4)
                break;

            const uint8_t type = buffer[0];
            const uint16_t manufacturer = (buffer[1] << 8) | (buffer[2]);
            const uint16_t product = (buffer[3] << 8) | (buffer[4]);
            buffer += 1 + 2 + 2;

            buffer += get_message_size(buffer, &size);
            char *name = malloc(size + 1);
            memcpy(name, buffer, size);
            name[size] = '\0';
            asc_log_info(MSG("CA: Module %s. 0x%02X 0x%04X 0x%04X")
                         , name, type, manufacturer, product);
            free(name);
            break;
        }
        default:
            asc_log_error(MSG("CA: Application Information. Wrong event:0x%08X"), tag);
            break;
    }
}

static void application_information_open(module_data_t *mod, uint8_t slot_id, uint16_t session_id)
{
    asc_log_debug(MSG("CA: %s(): slot_id:%d session_id:%d"), __FUNCTION__, slot_id, session_id);

    ca_session_t *session = &mod->slots[slot_id].sessions[session_id];
    session->event = application_information_event;
    ca_apdu_send(mod, slot_id, session_id, AOT_APPLICATION_INFO_ENQ, NULL, 0);
}

/*
 * Conditional Access Support
 */

typedef struct
{
    uint8_t caid_list_size;
    uint16_t *caid_list;
} conditional_access_data_t;

static void conditional_access_event(module_data_t *mod, uint8_t slot_id, uint16_t session_id)
{
    const uint32_t tag = ca_apdu_get_tag(mod, slot_id);
    switch(tag)
    {
        case AOT_CA_INFO:
        {
            uint16_t size = 0;
            const uint8_t *buffer = ca_apdu_get_buffer(mod, slot_id, &size);
            if(size < 2)
                break;

            conditional_access_data_t *data = mod->slots[slot_id].sessions[session_id].data;

            if(data->caid_list)
                free(data->caid_list);
            data->caid_list_size = size / 2;
            data->caid_list = calloc(data->caid_list_size, sizeof(uint16_t));

            for(int i = 0; i < data->caid_list_size; ++i)
            {
                const uint16_t caid = (buffer[0] << 8) | (buffer[1]);
                buffer += 2;
                data->caid_list[i] = caid;
                asc_log_info(MSG("CA: Module CAID:0x%04X"), caid);
            }

            // TODO: send PMT to CAM
            break;
        }
        case AOT_CA_UPDATE:
        case AOT_CA_PMT_REPLY:
            break;
        default:
            asc_log_error(MSG("CA: Conditional Access. Wrong event:0x%08X"), tag);
            break;
    }
}

static void conditional_access_close(module_data_t *mod, uint8_t slot_id, uint16_t session_id)
{
    ca_session_t *session = &mod->slots[slot_id].sessions[session_id];
    conditional_access_data_t *data = session->data;

    if(data->caid_list)
        free(data->caid_list);

    free(data);
}

static void conditional_access_open(module_data_t *mod, uint8_t slot_id, uint16_t session_id)
{
    asc_log_debug(MSG("CA: %s(): slot_id:%d session_id:%d"), __FUNCTION__, slot_id, session_id);

    ca_session_t *session = &mod->slots[slot_id].sessions[session_id];
    session->event = conditional_access_event;
    session->close = conditional_access_close;
    session->data = calloc(1, sizeof(conditional_access_data_t));

    ca_apdu_send(mod, slot_id, session_id, AOT_CA_INFO_ENQ, NULL, 0);
}

/*
 * Date-Time
 */

typedef struct
{
    uint8_t interval;
} date_time_data_t;

static void date_time_send(module_data_t *mod, uint8_t slot_id, uint16_t session_id)
{
    time_t ct = time(NULL);
    struct tm tm_gmtime;
    struct tm tm_localtime;

    if(!gmtime_r(&ct, &tm_gmtime))
        return;
    if(!localtime_r(&ct, &tm_localtime))
        return;

    const int year = tm_gmtime.tm_year;
    const int mon = tm_gmtime.tm_mon + 1;
    const int day = tm_gmtime.tm_mday;
    const int l = ((mon == 1) || (mon == 2)) ? 1 : 0;
    const int mjd = 14956 + day + ((int)((year - l) * 365.25))
                  + ((int)((mon + 1 + l * 12) * 30.6001));

    uint8_t buffer[8];

    const int mjd_n = htons(mjd);
    buffer[0] = (mjd_n >> 8) & 0xFF;
    buffer[1] = (mjd_n     ) & 0xFF;

#define dec_to_bcd(d) (((d / 10) << 4) + (d % 10))
    buffer[2] = dec_to_bcd(tm_gmtime.tm_hour);
    buffer[3] = dec_to_bcd(tm_gmtime.tm_min);
    buffer[4] = dec_to_bcd(tm_gmtime.tm_sec);
#undef dec_to_bcd

    const int gmtoff = tm_localtime.tm_gmtoff / 60;
    buffer[5] = (gmtoff >> 8) & 0xFF;
    buffer[6] = (gmtoff     ) & 0xFF;

    ca_apdu_send(mod, slot_id, session_id, AOT_DATE_TIME, buffer, 7);
}

static void date_time_event(module_data_t *mod, uint8_t slot_id, uint16_t session_id)
{
    const uint32_t tag = ca_apdu_get_tag(mod, slot_id);
    switch(tag)
    {
        case AOT_DATE_TIME_ENQ:
        {
            uint16_t size = 0;
            const uint8_t *buffer = ca_apdu_get_buffer(mod, slot_id, &size);

            date_time_data_t *data = mod->slots[slot_id].sessions[session_id].data;

            data->interval = (size > 0) ? buffer[0] : 0;

            date_time_send(mod, slot_id, session_id);
            break;
        }
        default:
            asc_log_error(MSG("CA: Date-Time. Wrong event:0x%08X"), tag);
            break;
    }
}

static void date_time_manage(module_data_t *mod, uint8_t slot_id, uint16_t session_id)
{
    date_time_data_t *data = mod->slots[slot_id].sessions[session_id].data;

    if(data->interval)
    {
        // TODO:
        // date_time_send(mod, slot_id, session_id);
    }
}

static void date_time_close(module_data_t *mod, uint8_t slot_id, uint16_t session_id)
{
    ca_session_t *session = &mod->slots[slot_id].sessions[session_id];
    free(session->data);
}

static void date_time_open(module_data_t *mod, uint8_t slot_id, uint16_t session_id)
{
    asc_log_debug(MSG("CA: %s(): slot_id:%d session_id:%d"), __FUNCTION__, slot_id, session_id);

    ca_session_t *session = &mod->slots[slot_id].sessions[session_id];
    session->event = date_time_event;
    session->manage = date_time_manage;
    session->close = date_time_close;
    session->data = calloc(1, sizeof(date_time_data_t));

    date_time_send(mod, slot_id, session_id);
}

/*
 * MMI
 */

/* Display Control Commands */
enum
{
    DCC_SET_MMI_MODE                                = 0x01,
    DCC_DISPLAY_CHARACTER_TABLE_LIST                = 0x02,
    DCC_INPUT_CHARACTER_TABLE_LIST                  = 0x03,
    DCC_OVERLAY_GRAPHICS_CHARACTERISTICS            = 0x04,
    DCC_FULL_SCREEN_GRAPHICS_CHARACTERISTICS        = 0x05
};

/* MMI Modes */
enum
{
    MM_HIGH_LEVEL                                   = 0x01,
    MM_LOW_LEVEL_OVERLAY_GRAPHICS                   = 0x02,
    MM_LOW_LEVEL_FULL_SCREEN_GRAPHICS               = 0x03
};

/* Display Reply IDs */
enum
{
    DRI_MMI_MODE_ACK                                = 0x01,
    DRI_LIST_DISPLAY_CHARACTER_TABLES               = 0x02,
    DRI_LIST_INPUT_CHARACTER_TABLES                 = 0x03,
    DRI_LIST_GRAPHIC_OVERLAY_CHARACTERISTICS        = 0x04,
    DRI_LIST_FULL_SCREEN_GRAPHIC_CHARACTERISTICS    = 0x05,
    DRI_UNKNOWN_DISPLAY_CONTROL_CMD                 = 0xF0,
    DRI_UNKNOWN_MMI_MODE                            = 0xF1,
    DRI_UNKNOWN_CHARACTER_TABLE                     = 0xF2
};

typedef struct
{
    int emtpy;
} mmi_data_t;

static void mmi_display_event(module_data_t *mod, uint8_t slot_id, uint16_t session_id)
{
    uint16_t size = 0;
    const uint8_t *buffer = ca_apdu_get_buffer(mod, slot_id, &size);
    if(!size)
        return;

    if(buffer[0] != DCC_SET_MMI_MODE)
    {
        asc_log_error(MSG("CA: MMI: unknown display command: 0x%02X"), buffer[0]);
        return;
    }

    if(size != 2 || buffer[1] != MM_HIGH_LEVEL)
    {
        asc_log_error(MSG("CA: MMI: unsupported mode:0x%02X size:%d"), buffer[1], size);
        return;
    }

    uint8_t response[2];
    response[0] = DRI_MMI_MODE_ACK;
    response[1] = MM_HIGH_LEVEL;
    ca_apdu_send(mod, slot_id, session_id, AOT_DISPLAY_REPLY, response, 2);
}

static void mmi_enq_event(module_data_t *mod, uint8_t slot_id, uint16_t session_id)
{
    // TODO: continue...
}

static void mmi_menu_event(module_data_t *mod, uint8_t slot_id, uint16_t session_id)
{
    // TODO: continue...
}

static void mmi_event(module_data_t *mod, uint8_t slot_id, uint16_t session_id)
{
    const uint32_t tag = ca_apdu_get_tag(mod, slot_id);
    switch(tag)
    {
        case AOT_DISPLAY_CONTROL:
            mmi_display_event(mod, slot_id, session_id);
            break;
        case AOT_ENQ:
            mmi_enq_event(mod, slot_id, session_id);
            break;
        case AOT_LIST_LAST:
        case AOT_MENU_LAST:
            mmi_menu_event(mod, slot_id, session_id);
            break;
        case AOT_CLOSE_MMI:
        {
            uint8_t response[4];
            response[0] = ST_CLOSE_SESSION_REQUEST;
            response[1] = 0x02;
            response[2] = (session_id >> 8) & 0xFF;
            response[3] = (session_id     ) & 0xFF;
            ca_tpdu_send(mod, slot_id, TT_DATA_LAST, response, 4);
            break;
        }
        default:
            asc_log_error(MSG("CA: MMI. Wrong event:0x%08X"), tag);
            break;
    }
}

static void mmi_close(module_data_t *mod, uint8_t slot_id, uint16_t session_id)
{
    ca_session_t *session = &mod->slots[slot_id].sessions[session_id];
    free(session->data);
}

static void mmi_open(module_data_t *mod, uint8_t slot_id, uint16_t session_id)
{
    asc_log_debug(MSG("CA: %s(): slot_id:%d session_id:%d"), __FUNCTION__, slot_id, session_id);

    ca_session_t *session = &mod->slots[slot_id].sessions[session_id];
    session->event = mmi_event;
    session->close = mmi_close;
    session->data = calloc(1, sizeof(mmi_data_t));
}

/*
 *      o      oooooooooo ooooooooo  ooooo  oooo
 *     888      888    888 888    88o 888    88
 *    8  88     888oooo88  888    888 888    88
 *   8oooo88    888        888    888 888    88
 * o88o  o888o o888o      o888ooo88    888oo88
 *
 */

static uint32_t ca_apdu_get_tag(module_data_t *mod, uint8_t slot_id)
{
    ca_slot_t *slot = &mod->slots[slot_id];

    if(slot->buffer_size >= SPDU_HEADER_SIZE + APDU_TAG_SIZE)
    {
        const uint8_t *buffer = &slot->buffer[SPDU_HEADER_SIZE];
        return (buffer[0] << 16) | (buffer[1] << 8) | (buffer[2]);
    }

    return 0;
}

static uint8_t * ca_apdu_get_buffer(module_data_t *mod, uint8_t slot_id, uint16_t *size)
{
    ca_slot_t *slot = &mod->slots[slot_id];
    if(slot->buffer_size < SPDU_HEADER_SIZE + APDU_TAG_SIZE + 1)
    {
        *size = 0;
        return NULL;
    }
    uint8_t *buffer = &slot->buffer[SPDU_HEADER_SIZE + APDU_TAG_SIZE];
    const uint8_t skip = get_message_size(buffer, size);
    return &buffer[skip];
}

static void ca_apdu_send(module_data_t *mod, uint8_t slot_id, uint16_t session_id
                         , uint32_t tag, const uint8_t *data, uint16_t size)
{
    uint8_t *buffer = malloc(size + SPDU_HEADER_SIZE + 12);
    uint32_t skip = 0;

    // SPDU Header
    buffer[0] = ST_SESSION_NUMBER;
    buffer[1] = 0x02;
    buffer[2] = (session_id >> 8) & 0xFF;
    buffer[3] = (session_id     ) & 0xFF;
    skip += SPDU_HEADER_SIZE;

    // APDU Header
    buffer[4] = (tag >> 16) & 0xFF;
    buffer[5] = (tag >>  8) & 0xFF;
    buffer[6] = (tag      ) & 0xFF;
    skip += APDU_TAG_SIZE;
    skip += set_message_size(&buffer[skip], size);

    // Data
    if(size)
    {
        memcpy(&buffer[skip], data, size);
        skip += size;
    }

    size = skip; // set total size
    skip = 0;

    while(skip < size)
    {
        const uint32_t block_size = size - skip;
        if(block_size > MAX_TPDU_SIZE)
        {
            ca_tpdu_send(mod, slot_id, TT_DATA_MORE, &buffer[skip], MAX_TPDU_SIZE);
            skip += MAX_TPDU_SIZE;
        }
        else
        {
            ca_tpdu_send(mod, slot_id, TT_DATA_LAST, &buffer[skip], block_size);
            skip += block_size;
            break;
        }
    }

    free(buffer);
}

/*
 *  oooooooo8 oooooooooo ooooooooo  ooooo  oooo
 * 888         888    888 888    88o 888    88
 *  888oooooo  888oooo88  888    888 888    88
 *         888 888        888    888 888    88
 * o88oooo888 o888o      o888ooo88    888oo88
 *
 */

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

    ca_tpdu_send(mod, slot_id, TT_DATA_LAST, response, 9);
}

static void ca_spdu_close(module_data_t *mod, uint8_t slot_id)
{
    ca_slot_t *slot = &mod->slots[slot_id];
    const uint16_t session_id = (slot->buffer[2] << 8) | slot->buffer[3];

    if(slot->sessions[session_id].close)
        slot->sessions[session_id].close(mod, slot_id, session_id);
    memset(&slot->sessions[session_id], 0, sizeof(ca_session_t));

    uint8_t response[8];
    response[0] = ST_CLOSE_SESSION_RESPONSE;
    response[1] = 0x03;
    response[2] = SPDU_STATUS_OPENED;
    response[3] = session_id >> 8;
    response[4] = session_id & 0xFF;

    ca_tpdu_send(mod, slot_id, TT_DATA_LAST, response, 5);
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
            resource_manager_open(mod, slot_id, session_id);
            break;
        case RI_APPLICATION_INFORMATION:
            application_information_open(mod, slot_id, session_id);
            break;
        case RI_CONDITIONAL_ACCESS_SUPPORT:
            conditional_access_open(mod, slot_id, session_id);
            break;
        case RI_DATE_TIME:
            date_time_open(mod, slot_id, session_id);
            break;
        case RI_MMI:
            mmi_open(mod, slot_id, session_id);
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
            const uint16_t session_id = (slot->buffer[2] << 8) | slot->buffer[3];
            if(slot->sessions[session_id].event)
                slot->sessions[session_id].event(mod, slot_id, session_id);
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
            if(slot->sessions[session_id].close)
                slot->sessions[session_id].close(mod, slot_id, session_id);
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

static void ca_tpdu_write(module_data_t *mod, uint8_t slot_id)
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

static void ca_tpdu_send(module_data_t *mod, uint8_t slot_id
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
        ca_tpdu_write(mod, slot_id);
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
            if(session->close)
                session->close(mod, slot_id, i);
            memset(session, 0, sizeof(ca_session_t));
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
                ca_tpdu_send(mod, i, TT_CREATE_TC, NULL, 0);
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
