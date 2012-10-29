/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#include <astra.h>
#include <modules/mpegts/mpegts.h>

#include <libavcodec/avcodec.h>

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(53,23,0)
#   error "libavcodec >=53.23.0 required"
#else

#include <time.h>
#include <fcntl.h>

#define LOG_MSG(_msg) "[mixaudio] " _msg

typedef enum
{
    MIXAUDIO_DIRECTION_NONE = 0,
    MIXAUDIO_DIRECTION_LL = 1,
    MIXAUDIO_DIRECTION_RR = 2,
} mixaudio_direction_t;

struct module_data_s
{
    MODULE_BASE();

    struct
    {
        int pid;
        const char *direction;
    } config;

    mixaudio_direction_t direction;

    // ffmpeg
    AVPacket davpkt;
    AVPacket eavpkt;

    AVFrame *frame;

    AVCodec *decoder;
    AVCodecContext *ctx_decode;
    AVCodec *encoder;
    AVCodecContext *ctx_encode;

    // PES packets
    mpegts_pes_t *pes_i; // input
    mpegts_pes_t *pes_o; // output

    // mpeg frame size
    size_t fsize;

    // splitted frame buffer
    uint8_t fbuffer[8192];
    size_t fbuffer_skip;

    int count;
};

// buffer16[i  ] - L
// buffer16[i+1] - R

void mix_buffer_ll(uint8_t *buffer, size_t buffer_size)
{
    uint16_t *buffer16 = (uint16_t *)buffer;
    buffer_size /= 2;

    for(int i = 0; i < buffer_size; i += 2)
        buffer16[i+1] = buffer16[i];
}

void mix_buffer_rr(uint8_t *buffer, size_t buffer_size)
{
    uint16_t *buffer16 = (uint16_t *)buffer;
    buffer_size /= 2;

    for(int i = 0; i < buffer_size; i += 2)
        buffer16[i] = buffer16[i+1];
}

/* callbacks */

static void pack_es(module_data_t *mod, uint8_t *data, size_t size)
{
    if(!mod->pes_o->buffer_size)
    {
        // copy PES header from original PES
        const size_t pes_hdr = 6 + 3 + mod->pes_i->buffer[8];
        memcpy(mod->pes_o->buffer, mod->pes_i->buffer, pes_hdr);
        mod->pes_o->buffer_size = pes_hdr;
    }

    mpegts_pes_add_data(mod->pes_o, data, size);

    if(mod->count == 1)
    {
        mpegts_pes_demux(mod->pes_o, stream_ts_send, mod);
        mod->pes_o->buffer_size = 0;
        mod->count = 0;
    }
    else
        ++mod->count;
}

static void transcode(module_data_t *mod, const uint8_t *data)
{
    mod->davpkt.data = (uint8_t *)data;
    mod->davpkt.size = mod->fsize;

    avcodec_get_frame_defaults(mod->frame);

    while(mod->davpkt.size > 0)
    {
        int got_frame = 0;
        const int len_d = avcodec_decode_audio4(mod->ctx_decode, mod->frame
                                                , &got_frame, &mod->davpkt);
        if(len_d < 0)
        {
            log_error(LOG_MSG("error while decoding"));
            return;
        }
        if(got_frame)
        {
            const int data_size
                = av_samples_get_buffer_size(NULL
                                             , mod->ctx_decode->channels
                                             , mod->frame->nb_samples
                                             , mod->ctx_decode->sample_fmt
                                             , 1);
            if(mod->direction == MIXAUDIO_DIRECTION_LL)
                mix_buffer_ll(mod->frame->data[0], data_size);
            else
                mix_buffer_rr(mod->frame->data[0], data_size);

            int got_packet = 0;
            av_init_packet(&mod->eavpkt);
            avcodec_encode_audio2(mod->ctx_encode, &mod->eavpkt
                                  , mod->frame, &got_packet);

            pack_es(mod, mod->eavpkt.data, mod->eavpkt.size);
            av_free_packet(&mod->eavpkt);
        }

        mod->davpkt.size -= len_d;
        mod->davpkt.data += len_d;
    }
}

static const uint16_t mpeg_brate[6][16] =
{
/* ID/BR */
    /* R */
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    /* V1,L1 */
    { 0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0},
    /* V1,L2 */
    { 0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0 },
    /* V1,L3 */
    { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0 },
    /* V2,L1 */
    { 0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0 },
    /* V2,L2/L3 */
    { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 }
};

static const uint8_t mpeg_brate_id[4][4] =
{
/* V/L      R  3  2  1 */
/* 2.5 */ { 0, 5, 5, 4 },
/*   R */ { 0, 0, 0, 0 },
/*   2 */ { 0, 5, 5, 4 },
/*   1 */ { 0, 3, 2, 1 },
};

const uint16_t mpeg_srate[4][4] = {
/*   V/SR */
/* 2.5 */ { 11025, 12000, 8000, 0 },
/*   R */ { 0, 0, 0, 0 },
/*   2 */ { 22050, 24000, 16000, 0 },
/*   1 */ { 44100, 48000, 32000, 0 }
};

static void mux_pes(module_data_t *mod, mpegts_pes_t *pes)
{
    const size_t pes_hdr = 6 + 3 + pes->buffer[8];
    const size_t es_size = pes->buffer_size - pes_hdr;

    const uint8_t *ptr = &pes->buffer[pes_hdr];
    const uint8_t *const ptr_end = ptr + es_size;

    if(!mod->fsize)
    {
        while(*ptr != 0xFF && ptr < ptr_end)
            ++ptr;

        const uint8_t mpeg_v = (ptr[1] & 0x18) >> 3; // version
        const uint8_t mpeg_l = (ptr[1] & 0x06) >> 1; // layer
        // const uint8_t mpeg_c = (ptr[3] & 0xC0) >> 6; // channel mode
        const uint8_t mpeg_br = (ptr[2] & 0xF0) >> 4; // bitrate
        const uint8_t mpeg_sr = (ptr[2] & 0x0C) >> 2; // sampling rate
        const uint8_t mpeg_p = (ptr[2] & 0x02) >> 1; // padding
        const uint8_t brate_id = mpeg_brate_id[mpeg_v][mpeg_l];
        const uint16_t br = mpeg_brate[brate_id][mpeg_br];
        const uint16_t sr = mpeg_srate[mpeg_v][mpeg_sr];
        const size_t fsize = (144 * br * 1000) / (sr + mpeg_p);
        if((ptr + fsize) < ptr_end
           && ptr[fsize] == 0xFF
           && (ptr[fsize + 1] & 0xF0) == 0xF0)
        {
            log_debug(LOG_MSG("set frame size = %lu"), fsize);
            mod->fsize = fsize;
        }
        else
            return;

        if(!mod->decoder)
        {
            enum CodecID codec_id = CODEC_ID_NONE;
            switch(mpeg_v)
            {
                case 0:
                case 2:
                    codec_id = CODEC_ID_MP2;
                    break;
                case 3:
                    codec_id = CODEC_ID_MP1;
                    break;
                default:
                    break;
            }

            int is_err = 1;
            do
            {
                mod->decoder = avcodec_find_decoder(codec_id);
                if(!mod->decoder)
                {
                    log_error(LOG_MSG("mp3 decoder is not found"));
                    break;
                }

                mod->ctx_decode = avcodec_alloc_context3(mod->decoder);
                if(avcodec_open2(mod->ctx_decode, mod->decoder, NULL) < 0)
                {
                    mod->decoder = NULL;
                    log_error(LOG_MSG("failed to open mp3 decoder"));
                    break;
                }

                is_err = 0;
            } while(0);

            if(is_err)
            {
                mod->direction = MIXAUDIO_DIRECTION_NONE;
                return;
            }
        }
    }

    if(mod->fbuffer_skip)
    {
        const size_t rlen = mod->fsize - mod->fbuffer_skip;
        memcpy(&mod->fbuffer[mod->fbuffer_skip], ptr, rlen);
        ptr += rlen;
        transcode(mod, mod->fbuffer);
    }

    do
    {
        transcode(mod, ptr);
        const uint8_t *const nptr = ptr + mod->fsize;
        if(nptr < ptr_end)
        {
            ptr = nptr;
            continue;
        }
        else if(nptr > ptr_end)
        {
            mod->fbuffer_skip = ptr_end - ptr;
            memcpy(mod->fbuffer, ptr, mod->fbuffer_skip);
        }
        else
            mod->fbuffer_skip = 0;

        break;
    } while(1);
}

static void callback_send_ts(module_data_t *mod, uint8_t *ts)
{
    if(mod->direction == MIXAUDIO_DIRECTION_NONE)
    {
        stream_ts_send(mod, ts);
        return;
    }

    const uint16_t pid = TS_PID(ts);
    if(pid == mod->config.pid)
        mpegts_pes_mux(mod->pes_i, ts, mux_pes, mod);
    else
        stream_ts_send(mod, ts);
}

/* methods */

static int method_attach(module_data_t *mod)
{
    stream_ts_attach(mod);
    return 0;
}

static int method_detach(module_data_t *mod)
{
    stream_ts_detach(mod);
    return 0;
}

/* required */

static void module_configure(module_data_t *mod)
{
    module_set_number(mod, "pid", 1, 0, &mod->config.pid);
    module_set_string(mod, "direction", 0, "LL", &mod->config.direction);
}

static void module_initialize(module_data_t *mod)
{
    stream_ts_init(mod, callback_send_ts, NULL, NULL, NULL, NULL);

    if(!strcasecmp(mod->config.direction, "LL"))
        mod->direction = MIXAUDIO_DIRECTION_LL;
    else if(!strcasecmp(mod->config.direction, "RR"))
        mod->direction = MIXAUDIO_DIRECTION_RR;

    avcodec_register_all();
    do
    {
        mod->encoder = avcodec_find_encoder(CODEC_ID_MP2);
        if(!mod->encoder)
        {
            log_error(LOG_MSG("mp3 encoder is not found"));
            break;
        }
        mod->ctx_encode = avcodec_alloc_context3(mod->encoder);
        mod->ctx_encode->bit_rate = 192000;
        mod->ctx_encode->sample_rate = 48000;
        mod->ctx_encode->channels = 2;
        mod->ctx_encode->sample_fmt = AV_SAMPLE_FMT_S16;
        mod->ctx_encode->channel_layout = av_get_channel_layout("stereo");
        if(avcodec_open2(mod->ctx_encode, mod->encoder, NULL) < 0)
        {
            log_error(LOG_MSG("failed to open mp3 encoder"));
            break;
        }

        mod->frame = avcodec_alloc_frame();

        av_init_packet(&mod->davpkt);

        mod->pes_i = mpegts_pes_init(MPEGTS_PACKET_AUDIO, mod->config.pid);
        mod->pes_o = mpegts_pes_init(MPEGTS_PACKET_AUDIO, mod->config.pid);
    } while(0);
}

static void module_destroy(module_data_t *mod)
{
    stream_ts_destroy(mod);

    if(mod->ctx_encode)
        avcodec_close(mod->ctx_encode);
    if(mod->ctx_decode)
        avcodec_close(mod->ctx_decode);
    if(mod->frame)
        av_free(mod->frame);
    av_free_packet(&mod->davpkt);

    if(mod->pes_i)
        mpegts_pes_destroy(mod->pes_i);
    if(mod->pes_o)
        mpegts_pes_destroy(mod->pes_o);
}

MODULE_METHODS()
{
    METHOD(attach)
    METHOD(detach)
};

MODULE(mixaudio)

#endif
