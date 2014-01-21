/*
 * Astra Module: MPEG-TS (MPTS Demux)
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

/*
 * Module Name:
 *      channel
 *
 * Module Options:
 *      upstream    - object, stream instance returned by module_instance:stream()
 *      name        - string, channel name
 *      pnr         - number, join PID related to the program number
 *      sdt         - boolean, join SDT table
 *      eit         - boolean, join EIT table
 *      map         - list, map PID by stream type, item format: "type=pid"
 *                    type: video, audio, rus, end... and other language code
 *                     pid: number identifier in range 32-8190
 */

#include <astra.h>

typedef struct
{
    char type[6];
    uint16_t origin_pid;
    uint16_t custom_pid;
    bool is_set;
} map_item_t;

struct module_data_t
{
    MODULE_LUA_DATA();
    MODULE_STREAM_DATA();

    /* Options */
    struct
    {
        const char *name;
        int pnr;
        int set_pnr;
        int sdt;
        int eit;
    } config;

    /* */
    asc_list_t *map;
    uint16_t pid_map[MAX_PID];
    uint8_t custom_ts[TS_PACKET_SIZE];

    int send_eit;

    mpegts_psi_t *pat;
    mpegts_psi_t *cat;
    mpegts_psi_t *pmt;
    mpegts_psi_t *sdt;

    mpegts_packet_type_t stream[MAX_PID];

    uint16_t tsid;
    mpegts_psi_t *custom_pat;
    mpegts_psi_t *custom_pmt;
    mpegts_psi_t *custom_sdt;

    /* */
    uint8_t sdt_original_section_id;
    uint8_t sdt_max_section_id;
    uint32_t *sdt_checksum_list;

    uint8_t eit_cc;
};

#define MSG(_msg) "[channel %s] " _msg, mod->config.name

static void stream_reload(module_data_t *mod)
{
    memset(mod->stream, 0, sizeof(mod->stream));

    for(int __i = 0; __i < MAX_PID; ++__i)
    {
        if(mod->__stream.pid_list[__i])
            module_stream_demux_leave_pid(mod, __i);
    }

    mod->stream[0] = MPEGTS_PACKET_PAT;
    mod->stream[1] = MPEGTS_PACKET_CAT;

    mod->pat->crc32 = 0;
    mod->cat->crc32 = 0;
    mod->pmt->crc32 = 0;

    module_stream_demux_join_pid(mod, 0x00);
    module_stream_demux_join_pid(mod, 0x01);

    if(mod->config.sdt)
    {
        mod->stream[0x11] = MPEGTS_PACKET_SDT;
        module_stream_demux_join_pid(mod, 0x11);
        if(mod->sdt_checksum_list)
        {
            free(mod->sdt_checksum_list);
            mod->sdt_checksum_list = NULL;
        }
    }

    if(mod->config.eit)
    {
        module_stream_demux_join_pid(mod, 0x12);
    }

    if(mod->map)
    {
        asc_list_for(mod->map)
        {
            map_item_t *map_item = asc_list_data(mod->map);
            map_item->is_set = false;
        }
    }
}

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
    if(crc32 == psi->crc32)
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

    // reload stream
    if(psi->crc32 != 0)
    {
        asc_log_warning(MSG("PAT changed. Reload stream info"));
        stream_reload(mod);
    }

    psi->crc32 = crc32;

    mod->tsid = PAT_GET_TSID(psi);

    const uint8_t *pointer = PAT_ITEMS_FIRST(psi);
    while(!PAT_ITEMS_EOL(psi, pointer))
    {
        const uint16_t pnr = PAT_ITEMS_GET_PNR(psi, pointer);
        const uint16_t pid = PAT_ITEMS_GET_PID(psi, pointer);

        if(pnr && (!mod->config.pnr || pnr == mod->config.pnr))
        {
            mod->config.pnr = pnr; // if(!mod->config.pnr)

            module_stream_demux_join_pid(mod, pid);
            mod->stream[pid] = MPEGTS_PACKET_PMT;
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

    mod->custom_pmt->pid = mod->pmt->pid;

    if(mod->config.set_pnr)
    {
        uint8_t *custom_pointer = PAT_ITEMS_FIRST(mod->custom_pat);
        custom_pointer[0] = mod->config.set_pnr >> 8;
        custom_pointer[1] = mod->config.set_pnr & 0xFF;
    }

    if(mod->map)
    {
        asc_list_for(mod->map)
        {
            map_item_t *map_item = asc_list_data(mod->map);
            if(map_item->is_set)
                continue;

            if(   (map_item->origin_pid && map_item->origin_pid == mod->pmt->pid)
               || (!strcmp(map_item->type, "pmt")) )
            {
                map_item->is_set = true;
                mod->pid_map[mod->pmt->pid] = map_item->custom_pid;

                uint8_t *custom_pointer = PAT_ITEMS_FIRST(mod->custom_pat);
                custom_pointer[2] = (custom_pointer[2] & ~0x1F)
                                  | ((map_item->custom_pid >> 8) & 0x1F);
                custom_pointer[3] = map_item->custom_pid & 0xFF;

                mod->custom_pmt->pid = map_item->custom_pid;
                break;
            }
        }
    }

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
        asc_log_error(MSG("CAT checksum mismatch"));
        return;
    }

    // reload stream
    if(psi->crc32 != 0)
    {
        asc_log_warning(MSG("CAT changed. Reload stream info"));
        stream_reload(mod);
        return;
    }

    psi->crc32 = crc32;

    const uint8_t *desc_pointer = CAT_DESC_FIRST(psi);
    while(!CAT_DESC_EOL(psi, desc_pointer))
    {
        if(desc_pointer[0] == 0x09)
        {
            const uint16_t ca_pid = DESC_CA_PID(desc_pointer);
            if(mod->stream[ca_pid] == MPEGTS_PACKET_UNKNOWN && ca_pid != NULL_TS_PID)
            {
                mod->stream[ca_pid] = MPEGTS_PACKET_CA;
                module_stream_demux_join_pid(mod, ca_pid);
            }
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

    if(psi->buffer[0] != 0x02)
        return;

    // check pnr
    const uint16_t pnr = PMT_GET_PNR(psi);
    if(pnr != mod->config.pnr)
        return;

    // check changes
    const uint32_t crc32 = PSI_GET_CRC32(psi);
    if(crc32 == psi->crc32)
    {
        mpegts_psi_demux(mod->custom_pmt
                         , (void (*)(void *, const uint8_t *))__module_stream_send
                         , &mod->__stream);
        return;
    }

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
    {
        asc_log_error(MSG("PMT checksum error"));
        return;
    }

    // reload stream
    if(psi->crc32 != 0)
    {
        asc_log_warning(MSG("PMT changed. Reload stream info"));
        stream_reload(mod);
        return;
    }

    psi->crc32 = crc32;

    const uint16_t pcr_pid = PMT_GET_PCR(psi);
    bool join_pcr = true;

    const uint8_t *desc_pointer = PMT_DESC_FIRST(psi);
    while(!PMT_DESC_EOL(psi, desc_pointer))
    {
        if(desc_pointer[0] == 0x09)
        {
            const uint16_t ca_pid = DESC_CA_PID(desc_pointer);
            if(mod->stream[ca_pid] == MPEGTS_PACKET_UNKNOWN && ca_pid != NULL_TS_PID)
            {
                mod->stream[ca_pid] = MPEGTS_PACKET_CA;
                module_stream_demux_join_pid(mod, ca_pid);
            }
        }
        PMT_DESC_NEXT(psi, desc_pointer);
    }

    const uint8_t *pointer = PMT_ITEMS_FIRST(psi);

    mod->custom_pmt->buffer_size = pointer - psi->buffer;
    memcpy(mod->custom_pmt->buffer, psi->buffer, mod->custom_pmt->buffer_size);

    if(mod->config.set_pnr)
    {
        PMT_SET_PNR(mod->custom_pmt, mod->config.set_pnr);
    }

    while(!PMT_ITEMS_EOL(psi, pointer))
    {
        const uint16_t pid = PMT_ITEM_GET_PID(psi, pointer);

        if(mod->pid_map[pid] == MAX_PID)
        { // skip filtered pid
            PMT_ITEMS_NEXT(psi, pointer);
            continue;
        }

        const uint16_t item_size = 5 + __PMT_ITEM_DESC_SIZE(pointer);
        uint8_t *custom_pointer = &mod->custom_pmt->buffer[mod->custom_pmt->buffer_size];
        memcpy(custom_pointer, pointer, item_size);
        mod->custom_pmt->buffer_size += item_size;

        module_stream_demux_join_pid(mod, pid);

        if(pid == pcr_pid)
            join_pcr = false;

        desc_pointer = PMT_ITEM_DESC_FIRST(pointer);
        while(!PMT_ITEM_DESC_EOL(pointer, desc_pointer))
        {
            if(desc_pointer[0] == 0x09)
            {
                const uint16_t ca_pid = DESC_CA_PID(desc_pointer);
                if(mod->stream[ca_pid] == MPEGTS_PACKET_UNKNOWN && ca_pid != NULL_TS_PID)
                {
                    mod->stream[ca_pid] = MPEGTS_PACKET_CA;
                    module_stream_demux_join_pid(mod, ca_pid);
                }
            }
            PMT_ITEM_DESC_NEXT(pointer, desc_pointer);
        }

        if(mod->map)
        {
            char type[6] = { 0 };
            switch(mpegts_pes_type(PMT_ITEM_GET_TYPE(psi, pointer)))
            {
                case MPEGTS_PACKET_VIDEO:
                {
                    strcpy(type, "video");
                    break;
                }
                case MPEGTS_PACKET_AUDIO:
                {
                    const uint8_t *desc_pointer = PMT_ITEM_DESC_FIRST(pointer);
                    while(!PMT_ITEM_DESC_EOL(pointer, desc_pointer))
                    {
                        if(desc_pointer[0] == 0x0A)
                        {
                            memcpy(type, &desc_pointer[2], 3);
                            type[3] = '\0';
                            break;
                        }
                        PMT_ITEM_DESC_NEXT(pointer, desc_pointer);
                    }
                    if(!type[0])
                        strcpy(type, "audio");
                    break;
                }
                default:
                    break;
            }

            asc_list_for(mod->map)
            {
                map_item_t *map_item = asc_list_data(mod->map);
                if(map_item->is_set)
                    continue;

                if(   (map_item->origin_pid && map_item->origin_pid == pid)
                   || (!strcmp(map_item->type, type)) )
                {
                    map_item->is_set = true;
                    mod->pid_map[pid] = map_item->custom_pid;

                    custom_pointer[1] = (custom_pointer[1] & ~0x1F)
                                      | ((map_item->custom_pid >> 8) & 0x1F);
                    custom_pointer[2] = map_item->custom_pid & 0xFF;
                    break;
                }
            }
        }

        PMT_ITEMS_NEXT(psi, pointer);
    }
    mod->custom_pmt->buffer_size += CRC32_SIZE;

    if(join_pcr)
        module_stream_demux_join_pid(mod, pcr_pid);

    if(mod->map)
    {
        if(mod->pid_map[pcr_pid])
        {
            const uint16_t custom_pcr_pid = mod->pid_map[pcr_pid];
            mod->custom_pmt->buffer[8] = (mod->custom_pmt->buffer[8] & ~0x1F)
                                       | ((custom_pcr_pid >> 8) & 0x1F);
            mod->custom_pmt->buffer[9] = custom_pcr_pid & 0xFF;
        }
    }

    PSI_SET_SIZE(mod->custom_pmt);
    PSI_SET_CRC32(mod->custom_pmt);
    mpegts_psi_demux(mod->custom_pmt
                     , (void (*)(void *, const uint8_t *))__module_stream_send
                     , &mod->__stream);
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

    const uint32_t crc32 = PSI_GET_CRC32(psi);

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
    {
        asc_log_error(MSG("SDT checksum error"));
        return;
    }

    // check changes
    if(!mod->sdt_checksum_list)
    {
        const uint8_t max_section_id = SDT_GET_LAST_SECTION_NUMBER(psi);
        mod->sdt_max_section_id = max_section_id;
        mod->sdt_checksum_list = calloc(max_section_id + 1, sizeof(uint32_t));
    }
    const uint8_t section_id = SDT_GET_SECTION_NUMBER(psi);
    if(section_id > mod->sdt_max_section_id)
    {
        asc_log_warning(MSG("SDT: section_number is greater then section_last_number"));
        return;
    }
    if(mod->sdt_checksum_list[section_id] == crc32)
    {
        if(mod->sdt_original_section_id == section_id)
        {
            mpegts_psi_demux(mod->custom_sdt
                             , (void (*)(void *, const uint8_t *))__module_stream_send
                             , &mod->__stream);
        }
        return;
    }

    if(mod->sdt_checksum_list[section_id] != 0)
    {
        asc_log_warning(MSG("SDT changed. Reload stream info"));
        stream_reload(mod);
        return;
    }

    mod->sdt_checksum_list[section_id] = crc32;

    const uint8_t *pointer = SDT_ITEMS_FIRST(psi);
    while(!SDT_ITEMS_EOL(psi, pointer))
    {
        if(SDT_ITEM_GET_SID(psi, pointer) == mod->config.pnr)
            break;

        SDT_ITEMS_NEXT(psi, pointer);
    }

    if(SDT_ITEMS_EOL(psi, pointer))
        return;

    mod->sdt_original_section_id = section_id;

    memcpy(mod->custom_sdt->buffer, psi->buffer, 11); // copy SDT header
    SDT_SET_SECTION_NUMBER(mod->custom_sdt, 0);
    SDT_SET_LAST_SECTION_NUMBER(mod->custom_sdt, 0);

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

    if(pid == NULL_TS_PID)
        return;

    if(pid == 0x12)
    {
        if(!mod->config.eit)
            return;

        bool is_pusi = (TS_PUSI(ts) != 0);
        uint8_t header_size = 0;

        if(is_pusi)
        {
            mod->send_eit = false;

            const uint8_t *payload = TS_PTR(ts);
            if(!payload)
                return;
            payload = payload + payload[0] + 1;
            header_size = payload - ts;

            const uint8_t table_id = payload[0];

            if(table_id == 0x4E || (table_id >= 0x50 && table_id <= 0x5F))
                mod->send_eit = (((payload[3] << 8) | payload[4]) == mod->config.pnr);
        }

        if(mod->send_eit)
        {
            memcpy(mod->custom_ts, ts, TS_PACKET_SIZE);
            mod->custom_ts[3] = (ts[3] & 0xF0) | mod->eit_cc;
            mod->eit_cc = (mod->eit_cc + 1) & 0x0F;
            if(mod->config.set_pnr && is_pusi)
            {
                mod->custom_ts[header_size + 3] = (mod->config.set_pnr >> 8) & 0xFF;
                mod->custom_ts[header_size + 4] = (mod->config.set_pnr     ) & 0xFF;
            }
            module_stream_send(mod, mod->custom_ts);
        }

        return;
    }

    switch(mod->stream[pid])
    {
        case MPEGTS_PACKET_UNKNOWN:
        case MPEGTS_PACKET_CA:
            break;
        case MPEGTS_PACKET_PAT:
            mpegts_psi_mux(mod->pat, ts, on_pat, mod);
            return;
        case MPEGTS_PACKET_CAT:
            mpegts_psi_mux(mod->cat, ts, on_cat, mod);
            break;
        case MPEGTS_PACKET_PMT:
            mpegts_psi_mux(mod->pmt, ts, on_pmt, mod);
            return;
        case MPEGTS_PACKET_SDT:
            mpegts_psi_mux(mod->sdt, ts, on_sdt, mod);
            return;
        default:
            break;
    }

    if(mod->pid_map[pid] == MAX_PID)
        return;

    if(mod->map)
    {
        const uint16_t custom_pid = mod->pid_map[pid];
        if(custom_pid)
        {
            // (((_ts[1] & 0x1f) << 8) | _ts[2])
            memcpy(mod->custom_ts, ts, TS_PACKET_SIZE);
            mod->custom_ts[1] = (mod->custom_ts[1] & ~0x1F)
                              | ((custom_pid >> 8) & 0x1F);
            mod->custom_ts[2] = custom_pid & 0xFF;
            module_stream_send(mod, mod->custom_ts);
            return;
        }
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

static void __parse_map_item(module_data_t *mod, const char *item)
{
    map_item_t *map_item = calloc(1, sizeof(map_item_t));
    uint8_t i = 0;
    for(; i < sizeof(map_item->type) && item[i] && item[i] != '='; ++i)
        map_item->type[i] = item[i];

    asc_assert(item[i] == '=', "option 'map' has wrong format");

    map_item->origin_pid = atoi(map_item->type);
    ++i; // skip '='
    map_item->custom_pid = atoi(&item[i]);

    asc_list_insert_tail(mod->map, map_item);
}

static void module_init(module_data_t *mod)
{
    module_stream_init(mod, on_ts);
    module_stream_demux_set(mod, NULL, NULL);

    module_option_string("name", &mod->config.name, NULL);
    asc_assert(mod->config.name != NULL, "[channel] option 'name' is required");

    module_option_number("pnr", &mod->config.pnr);
    module_option_number("set_pnr", &mod->config.set_pnr);

    mod->pat = mpegts_psi_init(MPEGTS_PACKET_PAT, 0);
    mod->cat = mpegts_psi_init(MPEGTS_PACKET_CAT, 1);
    mod->pmt = mpegts_psi_init(MPEGTS_PACKET_PMT, MAX_PID);
    mod->custom_pat = mpegts_psi_init(MPEGTS_PACKET_PAT, 0);
    mod->custom_pmt = mpegts_psi_init(MPEGTS_PACKET_PMT, MAX_PID);
    mod->stream[0] = MPEGTS_PACKET_PAT;
    module_stream_demux_join_pid(mod, 0);
    mod->stream[1] = MPEGTS_PACKET_CAT;
    module_stream_demux_join_pid(mod, 1);

    if(module_option_number("sdt", &mod->config.sdt) && mod->config.sdt)
    {
        mod->sdt = mpegts_psi_init(MPEGTS_PACKET_SDT, 0x11);
        mod->custom_sdt = mpegts_psi_init(MPEGTS_PACKET_SDT, 0x11);
        mod->stream[0x11] = MPEGTS_PACKET_SDT;
        module_stream_demux_join_pid(mod, 0x11);
    }

    if(module_option_number("eit", &mod->config.eit) && mod->config.eit)
        module_stream_demux_join_pid(mod, 0x12);

    lua_getfield(lua, 2, "map");
    if(!lua_isnil(lua, -1))
    {
        asc_assert(lua_type(lua, -1) == LUA_TTABLE, "option 'map' required table");

        mod->map = asc_list_init();
        const int map = lua_gettop(lua);
        for(lua_pushnil(lua); lua_next(lua, map); lua_pop(lua, 1))
        {
            const char *map_item = lua_tostring(lua, -1);
            __parse_map_item(mod, map_item);
        }
    }
    lua_pop(lua, 1); // map

    lua_getfield(lua, 2, "filter");
    if(!lua_isnil(lua, -1))
    {
        asc_assert(lua_type(lua, -1) == LUA_TTABLE, "option 'filter' required table");

        const int filter = lua_gettop(lua);
        for(lua_pushnil(lua); lua_next(lua, filter); lua_pop(lua, 1))
        {
            const int pid = lua_tonumber(lua, -1);
            mod->pid_map[pid] = MAX_PID;
        }
    }
    lua_pop(lua, 1); // filter
}

static void module_destroy(module_data_t *mod)
{
    module_stream_destroy(mod);

    mpegts_psi_destroy(mod->pat);
    mpegts_psi_destroy(mod->cat);
    mpegts_psi_destroy(mod->pmt);
    mpegts_psi_destroy(mod->custom_pat);
    mpegts_psi_destroy(mod->custom_pmt);

    if(mod->sdt)
    {
        mpegts_psi_destroy(mod->sdt);
        mpegts_psi_destroy(mod->custom_sdt);

        if(mod->sdt_checksum_list)
            free(mod->sdt_checksum_list);
    }

    if(mod->map)
    {
        for(asc_list_first(mod->map); !asc_list_eol(mod->map); asc_list_first(mod->map))
        {
            free(asc_list_data(mod->map));
            asc_list_remove_current(mod->map);
        }
        asc_list_destroy(mod->map);
    }
}

MODULE_STREAM_METHODS()
MODULE_LUA_METHODS()
{
    MODULE_STREAM_METHODS_REF()
};
MODULE_LUA_REGISTER(channel)
