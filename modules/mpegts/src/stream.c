/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#include <astra.h>
#include "../mpegts.h"

void mpegts_stream_destroy(mpegts_stream_t stream)
{
    for(int i = 0; i < MAX_PID; ++i)
    {
        mpegts_psi_t *psi = stream[i];
        if(!psi)
            continue;
        switch(psi->type)
        {
            case MPEGTS_PACKET_PAT:
                mpegts_pat_destroy(psi);
                break;
            case MPEGTS_PACKET_CAT:
                mpegts_cat_destroy(psi);
                break;
            case MPEGTS_PACKET_PMT:
                mpegts_pmt_destroy(psi);
                break;
            case MPEGTS_PACKET_SDT:
                mpegts_sdt_destroy(psi);
                break;
            default:
                mpegts_psi_destroy(psi);
                break;
        }
        stream[i] = NULL;
    }
}
