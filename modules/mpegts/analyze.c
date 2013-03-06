/*
 * Astra MPEG-TS Analyze Module
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

/*
 * Module Name:
 *      analyze
 *
 * Module Options:
 *      upstream    - object, stream instance returned by module_instance:stream()
 *      name        - string, analyzer name
 */

#include <astra.h>

struct module_data_t
{
    MODULE_STREAM_DATA();

    char *name;

    mpegts_psi_t *stream[MAX_PID];
};

#define MSG(_msg) "[analyze %s] " _msg, mod->name

static void on_pat(void *arg, mpegts_psi_t *psi)
{
    module_data_t *mod = arg;

    // check changes
    const uint32_t crc32 = PSI_GET_CRC32(psi);
    if(crc32 == psi->crc32)
        return;

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
    {
        asc_log_error(MSG("PAT checksum mismatch"));
        return;
    }

    psi->crc32 = crc32;

    asc_log_info(MSG("PAT: stream_id:%d"), PAT_GET_TSID(psi));
    const uint8_t *pointer = PAT_ITEMS_FIRST(psi);
    while(!PAT_ITEMS_EOL(psi, pointer))
    {
        const uint16_t pnr = PAT_ITEMS_GET_PNR(psi, pointer);
        const uint16_t pid = PAT_ITEMS_GET_PID(psi, pointer);
        if(!pnr)
            asc_log_info(MSG("PAT: pid:%4d NIT"), pid);
        else
        {
            asc_log_info(MSG("PAT: pid:%4d PMT pnr:%4d"), pid, pnr);
            mod->stream[pid] = mpegts_psi_init(MPEGTS_PACKET_PMT, pid);
        }

        PAT_ITEMS_NEXT(psi, pointer);
    }

    asc_log_info(MSG("PAT: crc32:0x%08X"), crc32);
}

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
    psi->crc32 = crc32;

    char desc_dump[256];
    const uint8_t *desc_pointer = CAT_DESC_FIRST(psi);
    while(!CAT_DESC_EOL(psi, desc_pointer))
    {
        mpegts_desc_to_string(desc_dump, sizeof(desc_dump), desc_pointer);
        asc_log_info(MSG("CAT: %s"), desc_dump);
        CAT_DESC_NEXT(psi, desc_pointer);
    }

    asc_log_info(MSG("CAT: crc32:0x%08X"), crc32);
}

static void on_pmt(void *arg, mpegts_psi_t *psi)
{
    module_data_t *mod = arg;

    // check changes
    const uint32_t crc32 = PSI_GET_CRC32(psi);
    if(crc32 == psi->crc32)
        return;

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
    {
        asc_log_error(MSG("PMT checksum mismatch"));
        return;
    }
    psi->crc32 = crc32;

    const uint16_t pnr = PMT_GET_PNR(psi);
    asc_log_info(MSG("PMT: pnr:%d"), pnr);

    char desc_dump[256];
    const uint8_t *desc_pointer = PMT_DESC_FIRST(psi);
    while(!PMT_DESC_EOL(psi, desc_pointer))
    {
        mpegts_desc_to_string(desc_dump, sizeof(desc_dump), desc_pointer);
        asc_log_info(MSG("PMT:     %s"), desc_dump);
        PMT_DESC_NEXT(psi, desc_pointer);
    }

    asc_log_info(MSG("PMT: pid:%4d PCR"), PMT_GET_PCR(psi));

    const uint8_t *pointer = PMT_ITEMS_FIRST(psi);
    while(!PMT_ITEMS_EOL(psi, pointer))
    {
        const uint16_t pid = PMT_ITEM_GET_PID(psi, pointer);
        const uint8_t type = PMT_ITEM_GET_TYPE(psi, pointer);
        asc_log_info(MSG("PMT: pid:%4d %s:0x%02X")
                     , pid, mpegts_type_name(mpegts_pes_type(type)), type);

        const uint8_t *desc_pointer = PMT_ITEM_DESC_FIRST(pointer);
        while(!PMT_ITEM_DESC_EOL(pointer, desc_pointer))
        {
            mpegts_desc_to_string(desc_dump, sizeof(desc_dump), desc_pointer);
            asc_log_info(MSG("PMT:     %s"), desc_dump);
            PMT_ITEM_DESC_NEXT(pointer, desc_pointer);
        }

        PMT_ITEMS_NEXT(psi, pointer);
    }

    asc_log_info(MSG("PMT: crc32:0x%08X"), crc32);
}

static void on_ts(module_data_t *mod, const uint8_t *ts)
{
    const uint16_t pid = TS_PID(ts);
    mpegts_psi_t *psi = mod->stream[pid];
    if(psi)
    {
        switch(psi->type)
        {
            case MPEGTS_PACKET_PAT:
                mpegts_psi_mux(psi, ts, on_pat, mod);
                break;
            case MPEGTS_PACKET_CAT:
                mpegts_psi_mux(psi, ts, on_cat, mod);
                break;
            case MPEGTS_PACKET_PMT:
                mpegts_psi_mux(psi, ts, on_pmt, mod);
                break;
            default:
                break;
        }
    }
}

/* module */

static void module_init(module_data_t *mod)
{
    const char *value = NULL;
    const int name_length = module_option_string("name", &value);
    if(!value)
    {
        asc_log_error("[analyze] option 'name' is required");
        astra_abort();
    }
    mod->name = malloc(name_length + 1);
    strcpy(mod->name, value);

    module_stream_init(mod, on_ts);

    mod->stream[0] = mpegts_psi_init(MPEGTS_PACKET_PAT, 0);
    mod->stream[1] = mpegts_psi_init(MPEGTS_PACKET_CAT, 1);
}

static void module_destroy(module_data_t *mod)
{
    module_stream_destroy(mod);

    for(int i = 0; i < MAX_PID; ++i)
    {
        if(mod->stream[i])
            mpegts_psi_destroy(mod->stream[i]);
    }

    free(mod->name);
}

MODULE_STREAM_METHODS()
MODULE_LUA_METHODS()
{
    MODULE_STREAM_METHODS_REF()
};
MODULE_LUA_REGISTER(analyze)
