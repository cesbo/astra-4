/*
 * Astra Module: DVB (en50221)
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

#include "ca.h"

#include <netinet/in.h>

#define MSG(_msg) "[dvb_ca %d:%d] " _msg, ca->adapter, ca->device

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

#define DATA_INDICATOR 0x80

static void ca_tpdu_send(  dvb_ca_t *ca, uint8_t slot_id
                         , uint8_t tag, const uint8_t *data, uint16_t size);

static void ca_apdu_send(  dvb_ca_t *ca, uint8_t slot_id, uint16_t session_id
                         , uint32_t tag, const uint8_t *data, uint16_t size);

static uint32_t ca_apdu_get_tag(dvb_ca_t *ca, uint8_t slot_id);
static uint8_t * ca_apdu_get_buffer(dvb_ca_t *ca, uint8_t slot_id, uint16_t *size);

/*
 *      o       oooooooo8 oooo   oooo      oo
 *     888     888         8888o  88     o888
 *    8  88     888oooooo  88 888o88      888
 *   8oooo88           888 88   8888 ooo  888
 * o88o  o888o o88oooo888 o88o    88 888 o888o
 *
 */

#define SIZE_INDICATOR 0x80

static uint8_t asn_1_encode(uint8_t *data, uint16_t size)
{
    if(size < SIZE_INDICATOR)
    {
        data[0] = size;
        return 1;
    }
    else if(size <= 0xFF)
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

static uint8_t asn_1_decode(const uint8_t *data, uint16_t *size)
{
    if(data[0] < SIZE_INDICATOR)
    {
        *size = data[0];
        return 1;
    }
    else if(data[0] == (SIZE_INDICATOR | 1))
    {
        *size = data[1];
        return 2;
    }
    else if(data[0] == (SIZE_INDICATOR | 2))
    {
        *size = (data[1] << 8) | (data[2]);
        return 3;
    }
    else
    {
        *size = 0;
        return 1;
    }
}

/*
 * oooooooooo  oooo     oooo
 *  888    888  8888o   888
 *  888oooo88   88 888o8 88
 *  888  88o    88  888  88
 * o888o  88o8 o88o  8  o88o
 *
 */

static void resource_manager_event(dvb_ca_t *ca, uint8_t slot_id, uint16_t session_id)
{
    const uint32_t tag = ca_apdu_get_tag(ca, slot_id);
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
            ca_apdu_send(ca, slot_id, session_id, AOT_PROFILE, (const uint8_t *)res, sizeof(res));
            break;
        }
        case AOT_PROFILE:
        {
            ca_apdu_send(ca, slot_id, session_id, AOT_PROFILE_CHANGE, NULL, 0);
            break;
        }
        default:
            asc_log_error(MSG("CA: Resource Manager. Wrong event:0x%08X"), tag);
            break;
    }
}

static void resource_manager_open(dvb_ca_t *ca, uint8_t slot_id, uint16_t session_id)
{
    ca_session_t *session = &ca->slots[slot_id].sessions[session_id];
    session->event = resource_manager_event;
    ca_apdu_send(ca, slot_id, session_id, AOT_PROFILE_ENQ, NULL, 0);
}

/*
 *      o      oooooooooo oooooooooo
 *     888      888    888 888    888
 *    8  88     888oooo88  888oooo88
 *   8oooo88    888        888
 * o88o  o888o o888o      o888o
 *
 */

static void application_information_event(dvb_ca_t *ca, uint8_t slot_id, uint16_t session_id)
{
    __uarg(session_id);

    const uint32_t tag = ca_apdu_get_tag(ca, slot_id);
    switch(tag)
    {
        case AOT_APPLICATION_INFO:
        {
            uint16_t size = 0;
            const uint8_t *buffer = ca_apdu_get_buffer(ca, slot_id, &size);
            if(size < 4)
                break;

            const uint8_t type = buffer[0];
            const uint16_t manufacturer = (buffer[1] << 8) | (buffer[2]);
            const uint16_t product = (buffer[3] << 8) | (buffer[4]);
            buffer += 1 + 2 + 2;

            buffer += asn_1_decode(buffer, &size);
            char *name = malloc(size + 1);
            memcpy(name, buffer, size);
            name[size] = '\0';
            asc_log_info(  MSG("CA: Module %s. 0x%02X 0x%04X 0x%04X")
                         , name, type, manufacturer, product);
            free(name);

            ca->status = CA_MODULE_STATUS_APP_INFO;
            break;
        }
        default:
            asc_log_error(MSG("CA: Application Information. Wrong event:0x%08X"), tag);
            break;
    }
}

static void application_information_open(dvb_ca_t *ca, uint8_t slot_id, uint16_t session_id)
{
    ca_session_t *session = &ca->slots[slot_id].sessions[session_id];
    session->event = application_information_event;
    ca_apdu_send(ca, slot_id, session_id, AOT_APPLICATION_INFO_ENQ, NULL, 0);
}

/*
 *   oooooooo8     o
 * o888     88    888
 * 888           8  88
 * 888o     oo  8oooo88
 *  888oooo88 o88o  o888o
 *
 */

/* en50221: ca_pmt_list_management */
enum
{
    CA_PMT_LM_MORE              = 0x00,
    CA_PMT_LM_FIRST             = 0x01,
    CA_PMT_LM_LAST              = 0x02,
    CA_PMT_LM_ONLY              = 0x03,
    CA_PMT_LM_ADD               = 0x04,
    CA_PMT_LM_UPDATE            = 0x05
};

/* en50221: ca_pmt_cmd_id */
enum
{
    CA_PMT_CMD_OK_DESCRAMBLING  = 0x01,
    CA_PMT_CMD_OK_MMI           = 0x02,
    CA_PMT_CMD_QUERY            = 0x03,
    CA_PMT_CMD_NOT_SELECTED     = 0x04
};

typedef struct
{
    uint8_t caid_list_size;
    uint16_t *caid_list;
} conditional_access_data_t;

// ADD: CA_PMT_LM_ADD, CA_PMT_CMD_OK_DESCRAMBLING
// UPD: CA_PMT_LM_UPDATE, CA_PMT_CMD_OK_DESCRAMBLING
// DEL: CA_PMT_LM_UPDATE, CA_PMT_CMD_NOT_SELECTED

static bool ca_pmt_check_caid(conditional_access_data_t *ca_data, uint16_t caid)
{
    for(int i = 0; i < ca_data->caid_list_size; ++i)
    {
        if(ca_data->caid_list[i] == caid)
            return true;
    }
    return false;
}

static uint16_t ca_pmt_copy_desc(  const uint8_t *src, uint16_t size, uint8_t *dst
                                 , conditional_access_data_t *ca_data)
{
    uint16_t ca_pmt_skip = 3; // info_length + ca_pmt_cmd_id
    uint16_t src_skip = 0;

    while(src_skip < size)
    {
        const uint8_t *desc = &src[src_skip];
        const uint8_t desc_size = desc[1] + 2;

        if(desc[0] == 0x09 && ca_pmt_check_caid(ca_data, DESC_CA_CAID(desc)))
        {
            memcpy(&dst[ca_pmt_skip], desc, desc_size);
            ca_pmt_skip += desc_size;
        }

        src_skip += desc_size;
    }

    if(ca_pmt_skip > 3)
    {
        const uint16_t info_length = ca_pmt_skip - 2; // except info_length
        dst[0] = 0xF0 | ((info_length >> 8) & 0x0F);
        dst[1] = (info_length & 0xFF);
        dst[2] = 0x00;
        return ca_pmt_skip;
    }
    else
    {
        dst[0] = 0xF0;
        dst[1] = 0x00;
        return 2;
    }
}

static bool ca_pmt_build(  dvb_ca_t *ca, ca_pmt_t *ca_pmt
                         , uint8_t slot_id, uint8_t session_id
                         , uint8_t list_manage, uint8_t cmd)
{
    bool is_caid = false;

    conditional_access_data_t *ca_data = ca->slots[slot_id].sessions[session_id].data;

    ca_pmt->buffer[0] = list_manage;
    ca_pmt->buffer[1] = (ca_pmt->pnr >> 8);
    ca_pmt->buffer[2] = (ca_pmt->pnr & 0xFF);
    ca_pmt->buffer[3] = 0xC1 | (PMT_GET_VERSION(ca_pmt->psi) << 1);

    uint16_t ca_size = 4; // skip header

    const uint8_t *pmt_info = PMT_DESC_FIRST(ca_pmt->psi);
    const uint16_t pmt_info_length = __PMT_DESC_SIZE(ca_pmt->psi);
    const uint16_t desc_size = ca_pmt_copy_desc(  pmt_info, pmt_info_length
                                                , &ca_pmt->buffer[ca_size], ca_data);
    if(desc_size > 2)
    {
        ca_pmt->buffer[ca_size + 2] = cmd;
        is_caid = true;
    }
    ca_size += desc_size;

    const uint8_t *pointer;
    PMT_ITEMS_FOREACH(ca_pmt->psi, pointer)
    {
        ca_pmt->buffer[ca_size + 0] = PMT_ITEM_GET_TYPE(ca_pmt->psi, pointer);
        const uint16_t pid = PMT_ITEM_GET_PID(ca_pmt->psi, pointer);
        ca_pmt->buffer[ca_size + 1] = 0xE0 | ((pid >> 8) & 0x1F);
        ca_pmt->buffer[ca_size + 2] = pid & 0xFF;
        ca_size += 3; // skip type and pid

        const uint8_t *es_info = PMT_ITEM_DESC_FIRST(pointer);
        const uint16_t es_info_length = __PMT_ITEM_DESC_SIZE(pointer);
        const uint16_t es_desc_size = ca_pmt_copy_desc(  es_info, es_info_length
                                                       , &ca_pmt->buffer[ca_size], ca_data);
        if(es_desc_size > 2)
        {
            ca_pmt->buffer[ca_size + 2] = cmd;
            is_caid = true;
        }
        ca_size += es_desc_size;
    }
    ca_pmt->buffer_size = ca_size;

    return is_caid;
}

static void ca_pmt_send(  dvb_ca_t *ca, ca_pmt_t *ca_pmt
                        , uint8_t slot_id, uint8_t session_id
                        , uint8_t list_manage, uint8_t cmd)
{
    if(asc_log_is_debug())
    {
        const char *lm_str = (list_manage == CA_PMT_LM_ADD) ? "add" : "update";
        const char *cmd_str = (cmd == CA_PMT_CMD_OK_DESCRAMBLING) ? "select" : "deselect";
        asc_log_debug(MSG("CA_PMT: pnr:%d %s:%s"), ca_pmt->pnr, lm_str, cmd_str);
    }

    ca_slot_t *slot = &ca->slots[slot_id];
    ca_session_t *session = &slot->sessions[session_id];
    if(session->resource_id != RI_CONDITIONAL_ACCESS_SUPPORT)
        return;

    if(!ca_pmt_build(ca, ca_pmt, slot_id, session_id, list_manage, cmd))
        return;

    ca_apdu_send(ca, slot_id, session_id, AOT_CA_PMT, ca_pmt->buffer, ca_pmt->buffer_size);
}

static void ca_pmt_send_all(dvb_ca_t *ca, uint8_t list_manage, uint8_t cmd)
{
    asc_list_for(ca->ca_pmt_list)
    {
        ca_pmt_t *ca_pmt = asc_list_data(ca->ca_pmt_list);

        for(int slot_id = 0; slot_id < ca->slots_num; ++slot_id)
        {
            ca_slot_t *slot = &ca->slots[slot_id];
            for(int session_id = 0; session_id < MAX_SESSIONS; ++session_id)
            {
                ca_session_t *session = &slot->sessions[session_id];
                if(session->resource_id != RI_CONDITIONAL_ACCESS_SUPPORT)
                    continue;

                ca_pmt_send(ca, ca_pmt, slot_id, session_id, list_manage, cmd);
            }
        }
    }
}

static void conditional_access_event(dvb_ca_t *ca, uint8_t slot_id, uint16_t session_id)
{
    const uint32_t tag = ca_apdu_get_tag(ca, slot_id);
    switch(tag)
    {
        case AOT_CA_INFO:
        {
            uint16_t size = 0;
            const uint8_t *buffer = ca_apdu_get_buffer(ca, slot_id, &size);
            if(size < 2)
                break;

            conditional_access_data_t *data = ca->slots[slot_id].sessions[session_id].data;

            if(data->caid_list)
                free(data->caid_list);
            data->caid_list_size = size / 2;
            data->caid_list = calloc(data->caid_list_size, sizeof(uint16_t));

            for(int i = 0; i < data->caid_list_size; ++i)
            {
                const uint16_t caid = (buffer[0] << 8) | (buffer[1]);
                buffer += 2;
                data->caid_list[i] = caid;
                asc_log_info(  MSG("CA: Module CAID:0x%04X (session %d:%d)")
                             , caid, slot_id, session_id);
            }

            ca->status = CA_MODULE_STATUS_CA_INFO;
            ca->pmt_check_delay = asc_utime();
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

static void conditional_access_close(dvb_ca_t *ca, uint8_t slot_id, uint16_t session_id)
{
    ca_session_t *session = &ca->slots[slot_id].sessions[session_id];
    conditional_access_data_t *data = session->data;

    if(data->caid_list)
        free(data->caid_list);

    free(data);
}

static void conditional_access_open(dvb_ca_t *ca, uint8_t slot_id, uint16_t session_id)
{
    ca_session_t *session = &ca->slots[slot_id].sessions[session_id];
    session->event = conditional_access_event;
    session->close = conditional_access_close;
    session->data = calloc(1, sizeof(conditional_access_data_t));

    ca_apdu_send(ca, slot_id, session_id, AOT_CA_INFO_ENQ, NULL, 0);
}

/*
 * ooooooooo   ooooooooooo
 *  888    88o 88  888  88
 *  888    888     888
 *  888    888     888
 * o888ooo88      o888o
 *
 */

typedef struct
{
    uint64_t interval;
    uint64_t last_utime;
} date_time_data_t;

static void date_time_send(dvb_ca_t *ca, uint8_t slot_id, uint16_t session_id)
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

    ca_apdu_send(ca, slot_id, session_id, AOT_DATE_TIME, buffer, 7);

    date_time_data_t *dt = ca->slots[slot_id].sessions[session_id].data;
    dt->last_utime = asc_utime();
}

static void date_time_event(dvb_ca_t *ca, uint8_t slot_id, uint16_t session_id)
{
    const uint32_t tag = ca_apdu_get_tag(ca, slot_id);
    switch(tag)
    {
        case AOT_DATE_TIME_ENQ:
        {
            uint16_t size = 0;
            const uint8_t *buffer = ca_apdu_get_buffer(ca, slot_id, &size);

            date_time_data_t *data = ca->slots[slot_id].sessions[session_id].data;

            data->interval = (size > 0) ? (buffer[0] * 1000000) : 0;

            date_time_send(ca, slot_id, session_id);
            break;
        }
        default:
            asc_log_error(MSG("CA: Date-Time. Wrong event:0x%08X"), tag);
            break;
    }
}

static void date_time_manage(dvb_ca_t *ca, uint8_t slot_id, uint16_t session_id)
{
    date_time_data_t *data = ca->slots[slot_id].sessions[session_id].data;

    const uint64_t cur = asc_utime();
    if(cur < data->last_utime)
    {
        // timetravel
        data->last_utime = cur;
        return;
    }

    const uint64_t next = data->last_utime + data->interval;
    if((data->interval > 0) && (cur > next))
        date_time_send(ca, slot_id, session_id);
}

static void date_time_close(dvb_ca_t *ca, uint8_t slot_id, uint16_t session_id)
{
    ca_session_t *session = &ca->slots[slot_id].sessions[session_id];
    free(session->data);
}

static void date_time_open(dvb_ca_t *ca, uint8_t slot_id, uint16_t session_id)
{
    ca_session_t *session = &ca->slots[slot_id].sessions[session_id];
    session->event = date_time_event;
    session->manage = date_time_manage;
    session->close = date_time_close;
    session->data = calloc(1, sizeof(date_time_data_t));

    date_time_send(ca, slot_id, session_id);
}

/*
 * oooo     oooo oooo     oooo ooooo
 *  8888o   888   8888o   888   888
 *  88 888o8 88   88 888o8 88   888
 *  88  888  88   88  888  88   888
 * o88o  8  o88o o88o  8  o88o o888o
 *
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

/* MMI Object Types */
enum
{
    EN50221_MMI_NONE                                = 0x00,
    EN50221_MMI_ENQ                                 = 0x01,
    EN50221_MMI_ANSW                                = 0x02,
    EN50221_MMI_MENU                                = 0x03,
    EN50221_MMI_MENU_ANSW                           = 0x04,
    EN50221_MMI_LIST                                = 0x05
};

typedef struct
{
    int object_type;
    union
    {
        struct
        {
            bool blind;
            char *text;
        } enq;
        struct
        {
            char *title;
            char *subtitle;
            char *bottom;
            asc_list_t *choices;
        } menu;
    } object;
} mmi_data_t;

static void mmi_free(mmi_data_t *mmi)
{
    switch(mmi->object_type)
    {
        case EN50221_MMI_ENQ:
            free(mmi->object.enq.text);
            break;
        case EN50221_MMI_MENU:
        case EN50221_MMI_LIST:
            free(mmi->object.menu.title);
            free(mmi->object.menu.subtitle);
            free(mmi->object.menu.bottom);
            for(asc_list_first(mmi->object.menu.choices)
                ; !asc_list_eol(mmi->object.menu.choices)
                ; asc_list_first(mmi->object.menu.choices))
            {
                free(asc_list_data(mmi->object.menu.choices));
                asc_list_remove_current(mmi->object.menu.choices);
            }
            asc_list_destroy(mmi->object.menu.choices);
            break;
        default:
            break;
    }

    mmi->object_type = EN50221_MMI_NONE;
}

static void mmi_send_menu_answer(dvb_ca_t *ca, uint8_t slot_id, uint16_t session_id, int choice)
{
    uint8_t answer[5];
    answer[0] = (AOT_MENU_ANSW >> 16) & 0xFF;
    answer[1] = (AOT_MENU_ANSW >>  8) & 0xFF;
    answer[2] = (AOT_MENU_ANSW      ) & 0xFF;
    answer[3] = 1;
    answer[4] = choice;
    ca_tpdu_send(ca, slot_id, TT_DATA_LAST, answer, sizeof(answer));
}

static void mmi_display_event(dvb_ca_t *ca, uint8_t slot_id, uint16_t session_id)
{
    uint16_t size = 0;
    const uint8_t *buffer = ca_apdu_get_buffer(ca, slot_id, &size);
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
    ca_apdu_send(ca, slot_id, session_id, AOT_DISPLAY_REPLY, response, 2);
}

static void mmi_enq_event(dvb_ca_t *ca, uint8_t slot_id, uint16_t session_id)
{
    uint16_t size = 0;
    const uint8_t *buffer = ca_apdu_get_buffer(ca, slot_id, &size);
    if(!size)
        return;

    mmi_data_t *mmi = ca->slots[slot_id].sessions[session_id].data;
    mmi_free(mmi);
    mmi->object_type = EN50221_MMI_ENQ;
    mmi->object.enq.blind = (buffer[0] & 0x01) ? true : false;
    buffer += 2; size -= 2; /* skip answer_text_length */
    mmi->object.enq.text = malloc(size + 1);
    memcpy(mmi->object.enq.text, buffer, size);
    mmi->object.enq.text[size] = '\0';
}

static uint16_t mmi_get_text(  dvb_ca_t *ca
                             , const uint8_t *buffer, uint16_t size
                             , char **text)
{
    uint32_t tag = 0;
    if(size >= 3)
        tag = (buffer[0] << 16) | (buffer[1] << 8) | (buffer[2]);

    if(tag != AOT_TEXT_LAST)
    {
        asc_log_error(MSG("CA: MMI: wrong text tag 0x%08X"), tag);
        *text = strdup("");
        return 0;
    }

    uint8_t skip = 3;
    *text = iso8859_decode(&buffer[skip + 1], buffer[skip]);

    return skip + 1 + buffer[skip];
}

static void mmi_menu_event(dvb_ca_t *ca, uint8_t slot_id, uint16_t session_id)
{
    uint16_t size = 0;
    const uint8_t *buffer = ca_apdu_get_buffer(ca, slot_id, &size);
    if(!size)
        return;

    mmi_data_t *mmi = ca->slots[slot_id].sessions[session_id].data;
    mmi_free(mmi);

    const uint32_t tag = ca_apdu_get_tag(ca, slot_id);
    if(tag == AOT_MENU_LAST)
    {
        mmi->object_type = EN50221_MMI_MENU;
        asc_log_debug(MSG("CA: MMI: Menu"));
    }
    else
    {
        mmi->object_type = EN50221_MMI_LIST;
        asc_log_debug(MSG("CA: MMI: List"));
    }

    uint16_t skip = 1; /* choice_nb */

    skip += mmi_get_text(ca, &buffer[skip], size - skip, &mmi->object.menu.title);
    skip += mmi_get_text(ca, &buffer[skip], size - skip, &mmi->object.menu.subtitle);
    skip += mmi_get_text(ca, &buffer[skip], size - skip, &mmi->object.menu.bottom);

    asc_log_debug(MSG("CA: MMI: Title: %s"), mmi->object.menu.title);
    asc_log_debug(MSG("CA: MMI: Subtitle: %s"), mmi->object.menu.subtitle);
    asc_log_debug(MSG("CA: MMI: Choice #0: Return"));
    mmi->object.menu.choices = asc_list_init();
    while(skip < size)
    {
        char *text = NULL;
        size_t choice_size = mmi_get_text(ca, &buffer[skip], size - skip, &text);
        asc_log_debug(MSG("CA: MMI: Choice #%d: %s"),
                      asc_list_size(mmi->object.menu.choices) + 1,
                      text);
        asc_list_insert_tail(mmi->object.menu.choices, text);
        skip += choice_size;

        if(choice_size == 0)
            break;
    }
    asc_log_debug(MSG("CA: MMI: Bottom: %s"), mmi->object.menu.bottom);

    mmi_send_menu_answer(ca, slot_id, session_id, 0);
    asc_log_debug(MSG("CA: MMI: Select #0"));
}

static void mmi_event(dvb_ca_t *ca, uint8_t slot_id, uint16_t session_id)
{
    const uint32_t tag = ca_apdu_get_tag(ca, slot_id);
    switch(tag)
    {
        case AOT_DISPLAY_CONTROL:
            mmi_display_event(ca, slot_id, session_id);
            break;
        case AOT_ENQ:
            mmi_enq_event(ca, slot_id, session_id);
            break;
        case AOT_LIST_LAST:
        case AOT_MENU_LAST:
            mmi_menu_event(ca, slot_id, session_id);
            break;
        case AOT_CLOSE_MMI:
        {
            uint8_t response[4];
            response[0] = ST_CLOSE_SESSION_REQUEST;
            response[1] = 0x02;
            response[2] = (session_id >> 8) & 0xFF;
            response[3] = (session_id     ) & 0xFF;
            ca_tpdu_send(ca, slot_id, TT_DATA_LAST, response, sizeof(response));
            break;
        }
        default:
            asc_log_error(MSG("CA: MMI: wrong event:0x%08X"), tag);
            break;
    }
}

static void mmi_close(dvb_ca_t *ca, uint8_t slot_id, uint16_t session_id)
{
    ca_session_t *session = &ca->slots[slot_id].sessions[session_id];
    mmi_free(session->data);
    free(session->data);
}

static void mmi_open(dvb_ca_t *ca, uint8_t slot_id, uint16_t session_id)
{
    ca_session_t *session = &ca->slots[slot_id].sessions[session_id];
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

static uint32_t ca_apdu_get_tag(dvb_ca_t *ca, uint8_t slot_id)
{
    ca_slot_t *slot = &ca->slots[slot_id];

    if(slot->buffer_size >= SPDU_HEADER_SIZE + APDU_TAG_SIZE)
    {
        const uint8_t *buffer = &slot->buffer[SPDU_HEADER_SIZE];
        return (buffer[0] << 16) | (buffer[1] << 8) | (buffer[2]);
    }

    return 0;
}

static uint8_t * ca_apdu_get_buffer(dvb_ca_t *ca, uint8_t slot_id, uint16_t *size)
{
    ca_slot_t *slot = &ca->slots[slot_id];
    if(slot->buffer_size < SPDU_HEADER_SIZE + APDU_TAG_SIZE + 1)
    {
        *size = 0;
        return NULL;
    }
    uint8_t *buffer = &slot->buffer[SPDU_HEADER_SIZE + APDU_TAG_SIZE];
    const uint8_t skip = asn_1_decode(buffer, size);
    return &buffer[skip];
}

static void ca_apdu_send(  dvb_ca_t *ca, uint8_t slot_id, uint16_t session_id
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
    skip += asn_1_encode(&buffer[skip], size);

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
            ca_tpdu_send(ca, slot_id, TT_DATA_MORE, &buffer[skip], MAX_TPDU_SIZE);
            skip += MAX_TPDU_SIZE;
        }
        else
        {
            ca_tpdu_send(ca, slot_id, TT_DATA_LAST, &buffer[skip], block_size);
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

static void ca_spdu_open(dvb_ca_t *ca, uint8_t slot_id)
{
    ca_slot_t *slot = &ca->slots[slot_id];

    uint16_t session_id = 0;
    ca_session_t *session = NULL;
    for(uint16_t i = 1; i < MAX_SESSIONS; ++i)
    {
        if(slot->sessions[i].resource_id == 0)
        {
            session_id = i;
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

    ca_tpdu_send(ca, slot_id, TT_DATA_LAST, response, 9);

    slot->pending_session_id = session_id;
}

static void ca_spdu_close(dvb_ca_t *ca, uint8_t slot_id)
{
    ca_slot_t *slot = &ca->slots[slot_id];
    const uint16_t session_id = (slot->buffer[2] << 8) | slot->buffer[3];

    if(slot->sessions[session_id].close)
        slot->sessions[session_id].close(ca, slot_id, session_id);
    memset(&slot->sessions[session_id], 0, sizeof(ca_session_t));

    uint8_t response[8];
    response[0] = ST_CLOSE_SESSION_RESPONSE;
    response[1] = 0x03;
    response[2] = SPDU_STATUS_OPENED;
    response[3] = session_id >> 8;
    response[4] = session_id & 0xFF;

    ca_tpdu_send(ca, slot_id, TT_DATA_LAST, response, 5);
}

static void ca_spdu_response_open(dvb_ca_t *ca, uint8_t slot_id)
{
    ca_slot_t *slot = &ca->slots[slot_id];

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
            resource_manager_open(ca, slot_id, session_id);
            break;
        case RI_APPLICATION_INFORMATION:
            application_information_open(ca, slot_id, session_id);
            break;
        case RI_CONDITIONAL_ACCESS_SUPPORT:
            conditional_access_open(ca, slot_id, session_id);
            break;
        case RI_DATE_TIME:
            date_time_open(ca, slot_id, session_id);
            break;
        case RI_MMI:
            mmi_open(ca, slot_id, session_id);
            break;
        case RI_HOST_CONTROL:
        default:
            asc_log_error(  MSG("CA: Slot %d session %d unknown resource %d")
                          , slot_id, session_id, resource_id);
            session->resource_id = 0;
            break;
    }
}

static void ca_spdu_event(dvb_ca_t *ca, uint8_t slot_id)
{
    ca_slot_t *slot = &ca->slots[slot_id];

    switch(slot->buffer[0])
    {
        case ST_SESSION_NUMBER:
        {
            if(slot->buffer_size <= 4)
                break;
            const uint16_t session_id = (slot->buffer[2] << 8) | slot->buffer[3];
            if(slot->sessions[session_id].event)
                slot->sessions[session_id].event(ca, slot_id, session_id);
            break;
        }
        case ST_OPEN_SESSION_REQUEST:
        {
            if(slot->buffer_size != 6 || slot->buffer[1] != 0x04)
                break;
            ca_spdu_open(ca, slot_id);
            break;
        }
        case ST_CLOSE_SESSION_REQUEST:
        {
            if(slot->buffer_size != 4 || slot->buffer[1] != 0x02)
                break;
            ca_spdu_close(ca, slot_id);
            break;
        }
        case ST_CREATE_SESSION_RESPONSE:
        {
            if(slot->buffer_size != 9 || slot->buffer[1] != 0x07)
                break;
            ca_spdu_response_open(ca, slot_id);
            break;
        }
        case ST_CLOSE_SESSION_RESPONSE:
        {
            if(slot->buffer_size != 5 || slot->buffer[1] != 0x03)
                break;
            const uint16_t session_id = (slot->buffer[3] << 8) | slot->buffer[4];
            if(slot->sessions[session_id].close)
                slot->sessions[session_id].close(ca, slot_id, session_id);
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

static void ca_tpdu_write(dvb_ca_t *ca, uint8_t slot_id)
{
    ca_slot_t *slot = &ca->slots[slot_id];
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

    if(write(ca->ca_fd, message->buffer, message->buffer_size) != (ssize_t)message->buffer_size)
        asc_log_error(MSG("CA: Slot %d write failed"), slot_id);
    else
        slot->is_busy = true;

    free(message);
}

static void ca_tpdu_send(  dvb_ca_t *ca, uint8_t slot_id
                         , uint8_t tag, const uint8_t *data, uint16_t size)
{
    ca_slot_t *slot = &ca->slots[slot_id];

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
            buffer_size += asn_1_encode(&buffer[buffer_size], size + 1);

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

    if(!slot->is_busy)
        ca_tpdu_write(ca, slot_id);
}

static void ca_tpdu_event(dvb_ca_t *ca)
{
    int buffer_size = read(ca->ca_fd, ca->ca_buffer, MAX_TPDU_SIZE);
    if(buffer_size < 5)
    {
        if(buffer_size == -1)
            asc_log_error(MSG("CA: read failed. [%s]"), strerror(errno));
        else
            asc_log_error(MSG("CA: read failed. size:%d"), buffer_size);

        return;
    }

    const uint8_t slot_id = ca->ca_buffer[1] - 1;
    const uint8_t tag = ca->ca_buffer[2];

    if(slot_id >= ca->slots_num)
    {
        asc_log_error(MSG("CA: read failed. wrong slot id %d"), slot_id);
        return;
    }

    ca_slot_t *slot = &ca->slots[slot_id];
    slot->is_busy = false;
    const bool has_data = (   (ca->ca_buffer[buffer_size - 4] == TT_SB)
                           && (ca->ca_buffer[buffer_size - 3] == 2)
                           && (ca->ca_buffer[buffer_size - 1] & DATA_INDICATOR));

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
            buffer_skip += asn_1_decode(&ca->ca_buffer[3], &message_size);
            if(message_size <= 1)
                break;

            ++buffer_skip;
            --message_size;

            if(slot->buffer_size + message_size > MAX_TPDU_SIZE)
            {
                asc_log_error(  MSG("tpdu buffer limit: buffer_size:%d message_size:%d")
                              , slot->buffer_size, message_size);
                slot->buffer_size = 0;
                break;
            }

            memcpy(&slot->buffer[slot->buffer_size]
                   , &ca->ca_buffer[buffer_skip]
                   , message_size);
            slot->buffer_size += message_size;

            if(tag == TT_DATA_LAST)
            {
                ca_spdu_event(ca, slot_id);
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

    if(!slot->is_busy && asc_list_size(slot->queue) > 0)
        ca_tpdu_write(ca, slot_id);

    if(!slot->is_busy && slot->pending_session_id)
    {
        const uint16_t session_id = slot->pending_session_id;
        slot->pending_session_id = 0;

        const uint32_t resource_id = slot->sessions[session_id].resource_id;
        switch(resource_id)
        {
            case RI_RESOURCE_MANAGER:
                resource_manager_open(ca, slot_id, session_id);
                break;
            case RI_APPLICATION_INFORMATION:
                application_information_open(ca, slot_id, session_id);
                break;
            case RI_CONDITIONAL_ACCESS_SUPPORT:
                conditional_access_open(ca, slot_id, session_id);
                break;
            case RI_DATE_TIME:
                date_time_open(ca, slot_id, session_id);
                break;
            case RI_MMI:
                mmi_open(ca, slot_id, session_id);
                break;
            case RI_HOST_CONTROL:
            default:
                asc_log_error(  MSG("CA: Slot %d session %d unknown resource %d")
                              , slot_id, session_id, resource_id);
                slot->sessions[session_id].resource_id = 0;
                break;
        }
    }

    if(!slot->is_busy && has_data)
        ca_tpdu_send(ca, slot_id, TT_RCV, NULL, 0);
}

/*
 *  oooooooo8 ooooo         ooooooo   ooooooooooo
 * 888         888        o888   888o 88  888  88
 *  888oooooo  888        888     888     888
 *         888 888      o 888o   o888     888
 * o88oooo888 o888ooooo88   88ooo88      o888o
 *
 */

static void ca_slot_reset(dvb_ca_t *ca, uint8_t slot_id)
{
    ca_slot_t *slot = &ca->slots[slot_id];

    if(ioctl(ca->ca_fd, CA_RESET, 1 << slot_id) != 0)
    {
        asc_log_error(MSG("CA: Slot %d CA_RESET failed"));
        return;
    }

    slot->is_active = false;
    slot->is_busy = false;
    slot->is_first_ca_pmt = true;

    for(asc_list_first(slot->queue); !asc_list_eol(slot->queue); asc_list_first(slot->queue))
    {
        free(asc_list_data(slot->queue));
        asc_list_remove_current(slot->queue);
    }

    for(int i = 1; i < MAX_SESSIONS; ++i)
    {
        ca_session_t *session = &slot->sessions[i];
        if(session->resource_id)
        {
            if(session->close)
                session->close(ca, slot_id, i);
            memset(session, 0, sizeof(ca_session_t));
        }
    }
}

static void ca_slot_loop(dvb_ca_t *ca)
{
    for(uint8_t slot_id = 0; slot_id < ca->slots_num; ++slot_id)
    {
        ca_slot_t *slot = &ca->slots[slot_id];

        ca_slot_info_t slot_info;
        slot_info.num = slot_id;
        if(ioctl(ca->ca_fd, CA_GET_SLOT_INFO, &slot_info) != 0)
        {
            asc_log_error(MSG("CA: Slot %d CA_GET_SLOT_INFO failed"), slot_id);
            continue;
        }

        if(!(slot_info.flags & CA_CI_MODULE_READY))
        {
            if(slot->is_active)
            {
                asc_log_warning(MSG("CA: Slot %d is not ready"), slot_id);
                ca_slot_reset(ca, slot_id);
            }
            continue;
        }
        else if(!slot->is_active)
        {
            if(!slot->is_busy)
            {
                asc_log_info(MSG("CA: Slot %d ready to go"), slot_id);
                ca_tpdu_send(ca, slot_id, TT_CREATE_TC, NULL, 0);
            }
            else
            {
                asc_log_warning(MSG("CA: Slot %d timeout. reset slot"), slot_id);
                ca_slot_reset(ca, slot_id);
            }
        }

        for(uint16_t session_id = 1; session_id < MAX_SESSIONS; ++session_id)
        {
            ca_session_t *session = &slot->sessions[session_id];
            if(session->resource_id && session->manage && !slot->is_busy)
                session->manage(ca, slot_id, session_id);
        }

        if(!slot->is_busy)
        {
            ca_tpdu_send(ca, slot_id, TT_DATA_LAST, NULL, 0);
            if(!slot->is_busy)
            {
                asc_log_error(MSG("CA: Slot %d failed to send poll command. reset slot"), slot_id);
                ca_slot_reset(ca, slot_id);
            }
        }
    }
}

/*
 * ooooooooooo  oooooooo8
 * 88  888  88 888
 *     888      888oooooo
 *     888             888
 *    o888o    o88oooo888
 *
 */

static void on_pat(void *arg, mpegts_psi_t *psi)
{
    dvb_ca_t *ca = arg;

    // check changes
    const uint32_t crc32 = PSI_GET_CRC32(psi);
    if(crc32 == psi->crc32)
        return;

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
    {
        asc_log_error(MSG("CA: PAT checksum error"));
        return;
    }

    // reload stream
    if(psi->crc32 != 0)
    {
        asc_log_warning(MSG("CA: PAT changed. Reload stream info"));
    }

    psi->crc32 = crc32;

    memset(ca->stream, 0, sizeof(ca->stream));
    ca->stream[0] = MPEGTS_PACKET_PAT;

    const uint8_t *pointer = PAT_ITEMS_FIRST(psi);
    while(!PAT_ITEMS_EOL(psi, pointer))
    {
        const uint16_t pnr = PAT_ITEM_GET_PNR(psi, pointer);

        if(pnr)
        {
            const uint16_t pid = PAT_ITEM_GET_PID(psi, pointer);
            ca->stream[pid] = MPEGTS_PACKET_PMT;
        }

        PAT_ITEMS_NEXT(psi, pointer);
    }
}

static void on_pmt(void *arg, mpegts_psi_t *psi)
{
    dvb_ca_t *ca = arg;

    if(psi->buffer[0] != 0x02)
        return;

    const uint32_t crc32 = PSI_GET_CRC32(psi);

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
    {
        asc_log_error(MSG("CA: PMT checksum error"));
        return;
    }

    const uint16_t pnr = PMT_GET_PNR(psi);

    if(pthread_mutex_trylock(&ca->ca_mutex) != 0)
        return;

    for(int i = 0; i < ca->pmt_count; ++i)
    {
        pmt_checksum_t *pmt_checksum = &ca->pmt_checksum_list[i];
        if(pmt_checksum->pnr != pnr)
            continue;

        if(pmt_checksum->crc == crc32)
            break;

        if(pmt_checksum->crc != 0)
            asc_log_warning(MSG("CA: PMT changed. Reload channel info"));

        pmt_checksum->crc = crc32;

        ca_pmt_t *ca_pmt = malloc(sizeof(ca_pmt_t));
        ca_pmt->pnr = pnr;
        ca_pmt->buffer_size = 0;
        ca_pmt->psi = mpegts_psi_init(MPEGTS_PACKET_PMT, psi->pid);
        memcpy(ca_pmt->psi, psi, sizeof(mpegts_psi_t));

        asc_list_for(ca->ca_pmt_list_new)
        {
            ca_pmt_t *ca_pmt_check = asc_list_data(ca->ca_pmt_list_new);
            if(ca_pmt_check->pnr == pnr)
            {
                free(ca_pmt_check);
                asc_list_remove_current(ca->ca_pmt_list_new);
                break;
            }
        }

        asc_list_insert_tail(ca->ca_pmt_list_new, ca_pmt);
        break;
    }

    pthread_mutex_unlock(&ca->ca_mutex);
}

void ca_on_ts(dvb_ca_t *ca, const uint8_t *ts)
{
    const uint16_t pid = TS_GET_PID(ts);
    switch(ca->stream[pid])
    {
        case MPEGTS_PACKET_PAT:
            mpegts_psi_mux(ca->pat, ts, on_pat, ca);
            return;
        case MPEGTS_PACKET_PMT:
            mpegts_psi_mux(ca->pmt, ts, on_pmt, ca);
            return;
        default:
            return;
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

void ca_append_pnr(dvb_ca_t *ca, uint16_t pnr)
{
    for(int i = 0; i < ca->pmt_count; ++i)
    {
        pmt_checksum_t *pmt_checksum = &ca->pmt_checksum_list[i];
        if(pmt_checksum->pnr == pnr)
            return;
    }

    ++ca->pmt_count;
    if(ca->pmt_count == 1)
    {
        ca->pmt_checksum_list = calloc(1, sizeof(pmt_checksum_t));
    }
    else
    {
        ca->pmt_checksum_list = realloc(ca->pmt_checksum_list
                                        , ca->pmt_count * sizeof(pmt_checksum_t));
    }
    ca->pmt_checksum_list[ca->pmt_count - 1].pnr = pnr;
}

void ca_remove_pnr(dvb_ca_t *ca, uint16_t pnr)
{
    if(ca->pmt_count == 0)
    {
        return;
    }
    else if(ca->pmt_count == 1)
    {
        free(ca->pmt_checksum_list);
        ca->pmt_checksum_list = NULL;
        ca->pmt_count = 0;
    }
    else
    {
        pmt_checksum_t *pmt_checksum_list = calloc(ca->pmt_count - 1, sizeof(pmt_checksum_t));
        int i = 0, j = 0;
        while(i < ca->pmt_count)
        {
            if(ca->pmt_checksum_list[i].pnr != pnr)
            {
                memcpy(&pmt_checksum_list[j], &ca->pmt_checksum_list[i]
                       , sizeof(pmt_checksum_t));
                ++j;
            }
            ++i;
        }
        --ca->pmt_count;
        free(ca->pmt_checksum_list);
        ca->pmt_checksum_list = pmt_checksum_list;
    }

    // TODO: remove PMT
}

void ca_open(dvb_ca_t *ca)
{
    char dev_name[32];
    sprintf(dev_name, "/dev/dvb/adapter%d/ca%d", ca->adapter, ca->device);

    ca->ca_fd = open(dev_name, O_RDWR | O_NONBLOCK);
    if(ca->ca_fd <= 0)
    {
        if(errno != ENOENT)
            asc_log_error(MSG("CA: failed to open ca [%s]"), strerror(errno));
        ca->ca_fd = 0;
        return;
    }

    ca_caps_t caps;
    memset(&caps, 0, sizeof(ca_caps_t));
    if(ioctl(ca->ca_fd, CA_GET_CAP, &caps) != 0)
    {
        asc_log_error(MSG("CA: CA_GET_CAP failed [%s]"), strerror(errno));
        ca_close(ca);
        return;
    }

    asc_log_info(MSG("CA: Slots:%d"), caps.slot_num);
    if(!caps.slot_num)
    {
        ca_close(ca);
        return;
    }
    ca->slots_num = caps.slot_num;

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
        ca_close(ca);
        return;
    }

    ca->slots = calloc(caps.slot_num, sizeof(ca_slot_t));

    for(uint8_t slot_id = 0; slot_id < ca->slots_num; ++slot_id)
    {
        ca_slot_t *slot = &ca->slots[slot_id];
        slot->queue = asc_list_init();

        ca_slot_reset(ca, slot_id);
    }

    pthread_mutex_init(&ca->ca_mutex, NULL);
    ca->ca_pmt_list = asc_list_init();
    ca->ca_pmt_list_new = asc_list_init();

    ca->pat = mpegts_psi_init(MPEGTS_PACKET_PAT, 0x00);
    ca->pmt = mpegts_psi_init(MPEGTS_PACKET_PMT, MAX_PID);

    ca->stream[0] = MPEGTS_PACKET_PAT;
}

void ca_close(dvb_ca_t *ca)
{
    memset(ca->stream, 0, sizeof(ca->stream));

    if(ca->pat)
        mpegts_psi_destroy(ca->pat);
    if(ca->pmt)
        mpegts_psi_destroy(ca->pmt);

    if(ca->ca_pmt_list)
    {
        ca_pmt_send_all(ca, CA_PMT_LM_UPDATE, CA_PMT_CMD_NOT_SELECTED);

        for(asc_list_first(ca->ca_pmt_list)
            ; !asc_list_eol(ca->ca_pmt_list)
            ; asc_list_first(ca->ca_pmt_list))
        {
            free(asc_list_data(ca->ca_pmt_list));
            asc_list_remove_current(ca->ca_pmt_list);
        }
        asc_list_destroy(ca->ca_pmt_list);
    }

    if(ca->ca_pmt_list_new)
    {
        for(asc_list_first(ca->ca_pmt_list_new)
            ; !asc_list_eol(ca->ca_pmt_list_new)
            ; asc_list_first(ca->ca_pmt_list_new))
        {
            free(asc_list_data(ca->ca_pmt_list_new));
            asc_list_remove_current(ca->ca_pmt_list_new);
        }

        asc_list_destroy(ca->ca_pmt_list_new);
    }

    pthread_mutex_destroy(&ca->ca_mutex);

    if(ca->ca_fd)
    {
        close(ca->ca_fd);
        ca->ca_fd = 0;
    }

    if(ca->slots)
    {
        for(int i = 0; i < ca->slots_num; ++i)
        {
            ca_slot_t *slot = &ca->slots[i];
            for(asc_list_first(slot->queue)
                ; !asc_list_eol(slot->queue)
                ; asc_list_first(slot->queue))
            {
                free(asc_list_data(slot->queue));
                asc_list_remove_current(slot->queue);
            }
            asc_list_destroy(slot->queue);
        }

        free(ca->slots);
        ca->slots = NULL;
    }
}

void ca_loop(dvb_ca_t *ca, int is_data)
{
    if(is_data)
    {
        ca_tpdu_event(ca);
        return;
    }

    ca_slot_loop(ca);

    switch(ca->status)
    {
        case CA_MODULE_STATUS_READY:
            break;
        case CA_MODULE_STATUS_CA_INFO:
        {
            const uint64_t current_time = asc_utime();
            if(current_time >= ca->pmt_check_delay + ca->pmt_delay)
            {
                ca->pmt_check_delay = current_time;
                ca->status = CA_MODULE_STATUS_READY;
            }
            return;
        }
        default:
            return;
    }

    if(asc_list_size(ca->ca_pmt_list_new) > 0)
    {
        const uint64_t current_time = asc_utime();
        if(current_time >= ca->pmt_check_delay + ca->pmt_delay)
        {
            ca->pmt_check_delay = current_time;

            pthread_mutex_lock(&ca->ca_mutex);

            ca_pmt_t *ca_pmt = NULL;
            asc_list_for(ca->ca_pmt_list_new)
            {
                ca_pmt_t *ca_pmt_check = asc_list_data(ca->ca_pmt_list_new);
                if(ca_pmt == NULL)
                {
                    ca_pmt = ca_pmt_check;
                }
                else
                {
                    if(ca_pmt_check->pnr < ca_pmt->pnr)
                        ca_pmt = ca_pmt_check;
                }
            }

            bool is_update = false;
            asc_list_for(ca->ca_pmt_list)
            {
                ca_pmt_t *ca_pmt_current = asc_list_data(ca->ca_pmt_list);
                if(ca_pmt_current->pnr == ca_pmt->pnr)
                {
                    is_update = true;
                    free(ca_pmt_current);
                    asc_list_remove_current(ca->ca_pmt_list);
                    break;
                }
            }

            for(int slot_id = 0; slot_id < ca->slots_num; ++slot_id)
            {
                ca_slot_t *slot = &ca->slots[slot_id];
                for(int session_id = 0; session_id < MAX_SESSIONS; ++session_id)
                {
                    ca_session_t *session = &slot->sessions[session_id];
                    if(session->resource_id != RI_CONDITIONAL_ACCESS_SUPPORT)
                        continue;

                    ca_pmt_send(  ca, ca_pmt, slot_id, session_id
                                , (is_update == true) ? CA_PMT_LM_UPDATE : CA_PMT_LM_ADD
                                , CA_PMT_CMD_OK_DESCRAMBLING);
                }
            }

            asc_list_remove_item(ca->ca_pmt_list_new, ca_pmt);
            asc_list_insert_tail(ca->ca_pmt_list, ca_pmt);

            pthread_mutex_unlock(&ca->ca_mutex);
        }
    }
}
