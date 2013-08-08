/*
 * Astra MixAudio Module
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

/*
 * Module Name:
 *      mixaudio
 *
 * Module Options:
 *      pid         - number, PID of the audio stream
 *      direction   - string, audio channel copying direction,
 *                    "LL" (by default) replace right channel to the left
 *                    "RR" replace left channel to the right
 */

#include <astra.h>
#include <time.h>
#include <fcntl.h>

#include <libavcodec/avcodec.h>

#if defined(LIBAVCODEC_VERSION_INT) && LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(53,23,0)

#define MSG(_msg) "[mixaudio] " _msg

typedef enum
{
    MIXAUDIO_DIRECTION_NONE = 0,
    MIXAUDIO_DIRECTION_LL = 1,
    MIXAUDIO_DIRECTION_RR = 2,
} mixaudio_direction_t;

struct module_data_t
{
    MODULE_LUA_DATA();
    MODULE_STREAM_DATA();

    int pid;
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
    int max_count;
};

// buffer16[i  ] - L
// buffer16[i+1] - R

void mix_buffer_ll(uint8_t *buffer, int buffer_size)
{
    uint16_t *buffer16 = (uint16_t *)buffer;
    buffer_size /= 2;

    for(int i = 0; i < buffer_size; i += 2)
        buffer16[i+1] = buffer16[i];
}

void mix_buffer_rr(uint8_t *buffer, int buffer_size)
{
    uint16_t *buffer16 = (uint16_t *)buffer;
    buffer_size /= 2;

    for(int i = 0; i < buffer_size; i += 2)
        buffer16[i] = buffer16[i+1];
}

/* callbacks */

static void pack_es(module_data_t *mod, uint8_t *data, size_t size)
{
    // hack to set PTS from source PES
    if(!mod->pes_o->buffer_size)
    {
        // copy PES header from original PES
        const size_t pes_hdr = 6 + 3 + mod->pes_i->buffer[8];
        memcpy(mod->pes_o->buffer, mod->pes_i->buffer, pes_hdr);
        mod->pes_o->buffer_size = pes_hdr;
    }

    mpegts_pes_add_data(mod->pes_o, data, size);
    ++mod->count;

    if(mod->count == mod->max_count)
    {
        mpegts_pes_demux(mod->pes_o
                         , (void (*)(void *, uint8_t *))__module_stream_send
                         , &mod->__stream);
        mod->pes_o->buffer_size = 0;
        mod->count = 0;
    }
}

static bool transcode(module_data_t *mod, const uint8_t *data)
{
    av_init_packet(&mod->davpkt);

    mod->davpkt.data = (uint8_t *)data;
    mod->davpkt.size = mod->fsize;

    while(mod->davpkt.size > 0)
    {
        int got_frame = 0;

        if(!mod->frame)
            mod->frame = avcodec_alloc_frame();
        else
            avcodec_get_frame_defaults(mod->frame);

        const int len_d = avcodec_decode_audio4(mod->ctx_decode, mod->frame
                                                , &got_frame, &mod->davpkt);

        if(len_d < 0)
        {
            asc_log_error(MSG("error while decoding"));
            mod->davpkt.size = 0;
            return false;
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
            mod->eavpkt.data = NULL;
            mod->eavpkt.size = 0;
            if(avcodec_encode_audio2(mod->ctx_encode, &mod->eavpkt, mod->frame, &got_packet) >= 0
               && got_packet)
            {
                // TODO: read http://bbs.rosoo.net/thread-14926-1-1.html
                pack_es(mod, mod->eavpkt.data, mod->eavpkt.size);
                av_free_packet(&mod->eavpkt);
            }
        }

        mod->davpkt.size -= len_d;
        mod->davpkt.data += len_d;
    }

    return true;
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

static void mux_pes(void *arg, mpegts_pes_t *pes)
{
    module_data_t *mod = arg;

    const size_t pes_hdr = 6 + 3 + pes->buffer[8];
    const size_t es_size = pes->buffer_size - pes_hdr;

    const uint8_t *ptr = &pes->buffer[pes_hdr];
    const uint8_t *const ptr_end = ptr + es_size;

    if(!mod->fbuffer_skip)
    {
        while(ptr < ptr_end - 1)
        {
            if(ptr[0] == 0xFF && (ptr[1] & 0xF0) == 0xF0)
                break;
            ++ptr;
        }
    }

    if(!mod->fsize)
    {
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
            asc_log_debug(MSG("set frame size = %lu"), fsize);
            mod->fsize = fsize;
            if(!(es_size % fsize))
                mod->max_count = es_size / fsize;
            else
                mod->max_count = 8;
        }
        else
            return;

        if(!mod->decoder)
        {
            enum AVCodecID codec_id = AV_CODEC_ID_NONE;
            switch(mpeg_v)
            {
                case 0:
                case 2:
                    codec_id = AV_CODEC_ID_MP2;
                    break;
                case 3:
                    codec_id = AV_CODEC_ID_MP1;
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
                    asc_log_error(MSG("mp3 decoder is not found"));
                    break;
                }

                mod->ctx_decode = avcodec_alloc_context3(mod->decoder);
                if(avcodec_open2(mod->ctx_decode, mod->decoder, NULL) < 0)
                {
                    mod->decoder = NULL;
                    asc_log_error(MSG("failed to open mp3 decoder"));
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
        mod->fbuffer_skip = 0;
        if(!transcode(mod, mod->fbuffer))
            return;
        ptr += rlen;
    }

    while(1)
    {
        const uint8_t *const nptr = ptr + mod->fsize;
        if(nptr < ptr_end)
        {
            if(!transcode(mod, ptr))
                break;
            ptr = nptr;
        }
        else if(nptr == ptr_end)
        {
            transcode(mod, ptr);
            break;
        }
        else /* nptr > ptr_end */
        {
            mod->fbuffer_skip = ptr_end - ptr;
            memcpy(mod->fbuffer, ptr, mod->fbuffer_skip);
            break;
        }
    }
}

static void on_ts(module_data_t *mod, const uint8_t *ts)
{
    if(mod->direction == MIXAUDIO_DIRECTION_NONE)
    {
        module_stream_send(mod, ts);
        return;
    }

    const uint16_t pid = TS_PID(ts);
    if(pid == mod->pid)
        mpegts_pes_mux(mod->pes_i, ts, mux_pes, mod);
    else
        module_stream_send(mod, ts);
}

/* required */

static void ffmpeg_log_callback(void *ptr, int level, const char *fmt, va_list vl)
{
    __uarg(ptr);
    void (*log_callback)(const char *, ...);

    switch(level)
    {
        case AV_LOG_INFO:
            log_callback = asc_log_info;
            break;
        case AV_LOG_WARNING:
            log_callback = asc_log_warning;
            break;
        case AV_LOG_DEBUG:
            log_callback = asc_log_debug;
            break;
        default:
            log_callback = asc_log_error;
            break;
    }

    char buffer[1024];
    const size_t len = vsnprintf(buffer, sizeof(buffer), fmt, vl);
    buffer[len - 1] = '\0';

    log_callback(buffer);
}

static void module_init(module_data_t *mod)
{
    module_stream_init(mod, on_ts);

    if(!module_option_number("pid", &mod->pid) || mod->pid <= 0)
    {
        asc_log_error(MSG("option 'pid' is required"));
        astra_abort();
    }
    const char *direction = NULL;
    if(!module_option_string("direction", &direction))
        direction = "LL";

    if(!strcasecmp(direction, "LL"))
        mod->direction = MIXAUDIO_DIRECTION_LL;
    else if(!strcasecmp(direction, "RR"))
        mod->direction = MIXAUDIO_DIRECTION_RR;

    av_log_set_callback(ffmpeg_log_callback);

    avcodec_register_all();
    do
    {
        mod->encoder = avcodec_find_encoder(CODEC_ID_MP2);
        if(!mod->encoder)
        {
            asc_log_error(MSG("mp3 encoder is not found"));
            astra_abort();
        }
        mod->ctx_encode = avcodec_alloc_context3(mod->encoder);
        mod->ctx_encode->bit_rate = 192000;
        mod->ctx_encode->sample_rate = 48000;
        mod->ctx_encode->channels = 2;
        mod->ctx_encode->sample_fmt = AV_SAMPLE_FMT_S16;
        mod->ctx_encode->channel_layout = av_get_channel_layout("stereo");
        if(avcodec_open2(mod->ctx_encode, mod->encoder, NULL) < 0)
        {
            asc_log_error(MSG("failed to open mp3 encoder"));
            astra_abort();
        }

        av_init_packet(&mod->davpkt);

        mod->pes_i = mpegts_pes_init(MPEGTS_PACKET_AUDIO, mod->pid);
        mod->pes_o = mpegts_pes_init(MPEGTS_PACKET_AUDIO, mod->pid);
        mod->pes_o->pts = 1;
    } while(0);
}

static void module_destroy(module_data_t *mod)
{
    module_stream_destroy(mod);

    if(mod->ctx_encode)
        avcodec_close(mod->ctx_encode);
    if(mod->ctx_decode)
        avcodec_close(mod->ctx_decode);
    if(mod->frame)
        avcodec_free_frame(&mod->frame);
    av_free_packet(&mod->davpkt);

    if(mod->pes_i)
        mpegts_pes_destroy(mod->pes_i);
    if(mod->pes_o)
        mpegts_pes_destroy(mod->pes_o);
}

MODULE_STREAM_METHODS()

MODULE_LUA_METHODS()
{
    MODULE_STREAM_METHODS_REF()
};

MODULE_LUA_REGISTER(mixaudio)

#else
#   error "libavcodec >=53.23.0 required"
#endif
