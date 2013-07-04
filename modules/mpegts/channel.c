/*
 * Astra Module Demux API
 * http://cesbo.com/astra
 *
 * Copyright (C) 2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

/*
 * Module Name:
 *      channel
 *
 * Module Options:
 *      upstream    - object, stream instance returned by module_instance:stream()
 *      name        - string, channel name
 *      pnr         - number, join PID related to the program number
 *      caid        - number, CAID to join CAS PID (CAT,EMM,ECM)
 *      sdt         - boolean, join SDT table
 *      eit         - boolean, join EIT table
 */

#include <astra.h>

struct module_data_t
{
    MODULE_LUA_DATA();
    MODULE_STREAM_DATA();

    /* Options */
    struct
    {
        const char *name;
        int pnr;
        int caid;
        int sdt;
        int eit;
    } config;

    /* */
    int send_eit;

    mpegts_psi_t *pat;
    mpegts_psi_t *cat;
    mpegts_psi_t *pmt;
    mpegts_psi_t *sdt;

    mpegts_packet_type_t _stream[MAX_PID];
    // mpegts_psi_t *stream[MAX_PID];

    uint16_t tsid;
    mpegts_psi_t *custom_pat;
    mpegts_psi_t *custom_sdt;
};

#define MSG(_msg) "[channel %s] " _msg, mod->config.name

/*
 * oooooooooo   o   ooooooooooo
 *  888    888 888  88  888  88
 *  888oooo88 8  88     888
 *  888      8oooo88    888
 * o888o   o88o  o888o o888o
 *
 */

static void on_pat(void *arg, mpegts_psi_t *psi)
{
    module_data_t *mod = arg;

    // check changes
    const uint32_t crc32 = PSI_GET_CRC32(psi);
    if(crc32 == psi->crc32 && mod->custom_pat->buffer_size > 0)
    {
        mpegts_psi_demux(mod->custom_pat
                         , (void (*)(void *, const uint8_t *))__module_stream_send
                         , &mod->__stream);
        return;
    }

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
    {
        asc_log_error(MSG("PAT checksum error"));
        return;
    }
    psi->crc32 = crc32;

    mod->tsid = PAT_GET_TSID(psi);

    const uint8_t *pointer = PAT_ITEMS_FIRST(psi);
    while(!PAT_ITEMS_EOL(psi, pointer))
    {
        const uint16_t pnr = PAT_ITEMS_GET_PNR(psi, pointer);
        const uint16_t pid = PAT_ITEMS_GET_PID(psi, pointer);
        if(pnr == mod->config.pnr)
        {
            module_stream_demux_join_pid(mod, pid);
            mod->pmt->pid = pid;
            mod->pmt->crc32 = 0;
            break;
        }

        PAT_ITEMS_NEXT(psi, pointer);
    }

    if(PAT_ITEMS_EOL(psi, pointer))
    {
        mod->custom_pat->buffer_size = 0;
        asc_log_error(MSG("PAT: stream with id %d is not found"), mod->config.pnr);
        return;
    }

    const uint8_t pat_version = PAT_GET_VERSION(mod->custom_pat) + 1;
    PAT_INIT(mod->custom_pat, mod->tsid, pat_version);
    memcpy(PAT_ITEMS_FIRST(mod->custom_pat), pointer, 4);
    mod->custom_pat->buffer_size = 8 + 4 + CRC32_SIZE;
    PSI_SET_SIZE(mod->custom_pat);
    PSI_SET_CRC32(mod->custom_pat);

    mpegts_psi_demux(mod->custom_pat
                     , (void (*)(void *, const uint8_t *))__module_stream_send
                     , &mod->__stream);
}

/*
 *   oooooooo8     o   ooooooooooo
 * o888     88    888  88  888  88
 * 888           8  88     888
 * 888o     oo  8oooo88    888
 *  888oooo88 o88o  o888o o888o
 *
 */

static void on_cat(void *arg, mpegts_psi_t *psi)
{
    module_data_t *mod = arg;

    // check changes
    const uint32_t crc32 = PSI_GET_CRC32(psi);
    if(crc32 == psi->crc32)
        return;

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
    {
        asc_log_error(MSG("PMT checksum error"));
        return;
    }
    psi->crc32 = crc32;

    const uint8_t *desc_pointer = CAT_DESC_FIRST(psi);
    while(!CAT_DESC_EOL(psi, desc_pointer))
    {
        if(desc_pointer[0] == 0x09
           && (mod->config.caid == 0xFFFF || DESC_CA_CAID(desc_pointer) == mod->config.caid))
        {
            module_stream_demux_join_pid(mod, DESC_CA_PID(desc_pointer));
        }

        CAT_DESC_NEXT(psi, desc_pointer);
    }
}

/*
 * oooooooooo oooo     oooo ooooooooooo
 *  888    888 8888o   888  88  888  88
 *  888oooo88  88 888o8 88      888
 *  888        88  888  88      888
 * o888o      o88o  8  o88o    o888o
 *
 */

static void on_pmt(void *arg, mpegts_psi_t *psi)
{
    module_data_t *mod = arg;

    // check pnr
    const uint16_t pnr = PMT_GET_PNR(psi);
    if(pnr != mod->config.pnr)
        return;

    // check changes
    const uint32_t crc32 = PSI_GET_CRC32(psi);
    if(crc32 == psi->crc32)
        return;

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
    {
        asc_log_error(MSG("PMT checksum error"));
        return;
    }
    psi->crc32 = crc32;

    if(mod->config.caid)
    {
        const uint8_t *desc_pointer = PMT_DESC_FIRST(psi);
        while(!PMT_DESC_EOL(psi, desc_pointer))
        {
            if(desc_pointer[0] == 0x09
               && (mod->config.caid == 0xFFFF || DESC_CA_CAID(desc_pointer) == mod->config.caid))
            {
                module_stream_demux_join_pid(mod, DESC_CA_PID(desc_pointer));
            }

            PMT_DESC_NEXT(psi, desc_pointer);
        }
    }

    const uint8_t *pointer = PMT_ITEMS_FIRST(psi);
    while(!PMT_ITEMS_EOL(psi, pointer))
    {
        const uint16_t pid = PMT_ITEM_GET_PID(psi, pointer);

        if(mod->config.caid)
        {
            const uint8_t *desc_pointer = PMT_ITEM_DESC_FIRST(pointer);
            while(!PMT_ITEM_DESC_EOL(pointer, desc_pointer))
            {
                if(desc_pointer[0] == 0x09
                   && (mod->config.caid == 0xFFFF
                       || DESC_CA_CAID(desc_pointer) == mod->config.caid))
                {
                    module_stream_demux_join_pid(mod, DESC_CA_PID(desc_pointer));
                }

                PMT_ITEM_DESC_NEXT(pointer, desc_pointer);
            }
        }

        module_stream_demux_join_pid(mod, pid);

        PMT_ITEMS_NEXT(psi, pointer);
    }
}

/*
 *  oooooooo8 ooooooooo   ooooooooooo
 * 888         888    88o 88  888  88
 *  888oooooo  888    888     888
 *         888 888    888     888
 * o88oooo888 o888ooo88      o888o
 *
 */

static void on_sdt(void *arg, mpegts_psi_t *psi)
{
    module_data_t *mod = arg;

    if(psi->buffer[0] != 0x42)
        return;

    if(mod->tsid != SDT_GET_TSID(psi))
        return;

    // check changes
    const uint32_t crc32 = PSI_GET_CRC32(psi);
    if(crc32 == psi->crc32 && mod->custom_sdt->buffer_size > 0)
    {
        mpegts_psi_demux(mod->custom_sdt
                         , (void (*)(void *, const uint8_t *))__module_stream_send
                         , &mod->__stream);
        return;
    }

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
    {
        asc_log_error(MSG("SDT checksum error"));
        return;
    }
    psi->crc32 = crc32;

    const uint8_t *pointer = SDT_ITEMS_FIRST(psi);
    while(!SDT_ITEMS_EOL(psi, pointer))
    {
        if(SDT_ITEM_GET_SID(psi, pointer) == mod->config.pnr)
            break;

        SDT_ITEMS_NEXT(psi, pointer);
    }

    if(SDT_ITEMS_EOL(psi, pointer))
    {
        mod->custom_sdt->buffer_size = 0;
        asc_log_error(MSG("SDT: stream with id %d is not found"), mod->config.pnr);
        return;
    }

    memcpy(mod->custom_sdt->buffer, psi->buffer, 11);
    const uint16_t item_length = __SDT_ITEM_DESC_SIZE(pointer) + 5;
    memcpy(&mod->custom_sdt->buffer[11], pointer, item_length);
    const uint16_t section_length = item_length + 8 + CRC32_SIZE;
    mod->custom_sdt->buffer_size = 3 + section_length;
    PSI_SET_SIZE(mod->custom_sdt);
    PSI_SET_CRC32(mod->custom_sdt);

    mpegts_psi_demux(mod->custom_sdt
                     , (void (*)(void *, const uint8_t *))__module_stream_send
                     , &mod->__stream);
}

/*
 * ooooooooooo  oooooooo8
 * 88  888  88 888
 *     888      888oooooo
 *     888             888
 *    o888o    o88oooo888
 *
 */

static void on_ts(module_data_t *mod, const uint8_t *ts)
{
    const uint16_t pid = TS_PID(ts);
    if(!module_stream_demux_check_pid(mod, pid))
        return;

    if(!mod->config.pnr)
    {
        module_stream_send(mod, ts);
        return;
    }

    if(pid == 0x12)
    {
        if(!mod->config.eit)
            return;

        if(TS_PUSI(ts))
        {
            const uint8_t *payload = TS_PTR(ts);
            payload = payload + payload[0] + 1;

            mod->send_eit = 0;

            const uint8_t table_id = payload[0];

            if(table_id == 0x4E || (table_id >= 0x50 && table_id <= 0x5F))
                mod->send_eit = (((payload[3] << 8) | payload[4]) == mod->config.pnr);
        }

        if(mod->send_eit)
            module_stream_send(mod, ts);

        return;
    }

    switch(mod->_stream[pid])
    {
        case MPEGTS_PACKET_UNKNOWN:
            break;
        case MPEGTS_PACKET_PAT:
            mpegts_psi_mux(mod->pat, ts, on_pat, mod);
            return;
        case MPEGTS_PACKET_CAT:
            mpegts_psi_mux(mod->cat, ts, on_cat, mod);
            break;
        case MPEGTS_PACKET_PMT:
            mpegts_psi_mux(mod->pmt, ts, on_pmt, mod);
            break;
        case MPEGTS_PACKET_SDT:
            mpegts_psi_mux(mod->sdt, ts, on_sdt, mod);
            break;
        default:
            break;
    }

    module_stream_send(mod, ts);
}

/*
 * oooo     oooo  ooooooo  ooooooooo  ooooo  oooo ooooo       ooooooooooo
 *  8888o   888 o888   888o 888    88o 888    88   888         888    88
 *  88 888o8 88 888     888 888    888 888    88   888         888ooo8
 *  88  888  88 888o   o888 888    888 888    88   888      o  888    oo
 * o88o  8  o88o  88ooo88  o888ooo88    888oo88   o888ooooo88 o888ooo8888
 *
 */

static void join_pid(module_data_t *mod, uint16_t pid)
{
    module_stream_demux_join_pid(mod, pid);
}

static void leave_pid(module_data_t *mod, uint16_t pid)
{
    module_stream_demux_leave_pid(mod, pid);
}

static void module_init(module_data_t *mod)
{
    module_stream_init(mod, on_ts);
    module_stream_demux_set(mod, join_pid, leave_pid);

    module_option_string("name", &mod->config.name);
    asc_assert(mod->config.name != NULL, "[channel] option 'name' is required");

    if(module_option_number("pnr", &mod->config.pnr))
    {
        mod->pat = mpegts_psi_init(MPEGTS_PACKET_PAT, 0);
        mod->pmt = mpegts_psi_init(MPEGTS_PACKET_PMT, MAX_PID);
        mod->custom_pat = mpegts_psi_init(MPEGTS_PACKET_PAT, 0);
        mod->_stream[0] = MPEGTS_PACKET_PAT;
        module_stream_demux_join_pid(mod, 0x00);
    }

    if(module_option_number("caid", &mod->config.caid) && mod->config.caid == 1)
    {
        mod->_stream[1] = MPEGTS_PACKET_CAT;
        module_stream_demux_join_pid(mod, 0x01);
    }

    if(module_option_number("sdt", &mod->config.sdt))
    {
        mod->sdt = mpegts_psi_init(MPEGTS_PACKET_SDT, 0x11);
        mod->custom_sdt = mpegts_psi_init(MPEGTS_PACKET_SDT, 0x11);
        mod->_stream[0x11] = MPEGTS_PACKET_SDT;
        module_stream_demux_join_pid(mod, 0x11);
    }

    if(module_option_number("eit", &mod->config.eit))
        module_stream_demux_join_pid(mod, 0x12);

    // TODO: parse options
}

static void module_destroy(module_data_t *mod)
{
    module_stream_destroy(mod);

    if(mod->pat)
    {
        mpegts_psi_destroy(mod->pat);
        mpegts_psi_destroy(mod->pmt);
        mpegts_psi_destroy(mod->custom_pat);
    }

    if(mod->cat)
    {
        mpegts_psi_destroy(mod->cat);
    }

    if(mod->sdt)
    {
        mpegts_psi_destroy(mod->sdt);
        mpegts_psi_destroy(mod->custom_sdt);
    }
}

MODULE_STREAM_METHODS()
MODULE_LUA_METHODS()
{
    MODULE_STREAM_METHODS_REF()
};
MODULE_LUA_REGISTER(channel)
