/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#if !defined(__linux)
#   error "DVB module for linux only"
#else

#include <astra.h>

#include <sys/time.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include <linux/dvb/version.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>

#if (DVB_API_VERSION < 5)
#   error "Please, update DVB drivers"
#endif

#define TS_PACKET_SIZE 188
#define MAX_PID 8192
#define LOG_MSG(_msg) "[dvb_input %d:%d] " _msg \
                      , mod->config.adapter, mod->config.device
#define STAT_INTERVAL (2 * 1000) // TODO: moved to thread (STATUS_INTERVAL)

// seconds
#define STATUS_INTERVAL 1
#define RETUNE_INTERVAL 5

#define BUFFER_SIZE (1022 * TS_PACKET_SIZE)

typedef enum
{
    DVB_TYPE_S = 1,
    DVB_TYPE_S2 = 2,
} dvb_type_t;

typedef enum
{
    TP_POL_H = 1, // H, L
    TP_POL_V = 2, // V, R
} tp_pol_t;

enum
{
    MESSAGE_LOCK = 1,
    MESSAGE_ERROR = 2,
    MESSAGE_RETUNE = 3,
};

struct module_data_s
{
    MODULE_BASE();
    MODULE_EVENT_BASE();

    struct
    {
        int adapter;
        int device;
        int diseqc;

        char *_type; // s, s2
        dvb_type_t type;

        int lnb_sharing;

        char *_lnb;
        struct
        {
            int lof1;
            int lof2;
            int slof;
        } lnb;

        char *_tp;
        struct
        {
            int freq;
            tp_pol_t pol;
            int srate;
        } tp; // transponder

        int budget;
        int buffer_size; // dvr ring buffer
    } config;

    /* frontend */

    int fe_fd;

    thread_t *thread;
    stream_t *thread_stream;

    struct
    {
        const char *error_message;

        fe_status_t fe;
        int lock;
        int signal;
        int snr;

        int ber;
        int unc;
    } status;
    int is_lock; // to send event
    int is_retune; // to demux bounce

    /* dvr */

    int dvr_fd;

    struct
    {
        char dev_name[32]; // demux dev name
        int fd_8192; // for budget mode, pid 8192
        int fd_list[MAX_PID];
    } demux;

    int join_list[MAX_PID];

    uint32_t bitrate;

    uint32_t stat_count;
    void *stat_timer;

    uint8_t buffer[BUFFER_SIZE];
};

/* frontend */

/* in thread function */
static int diseqc_setup(module_data_t *mod
                        , int hiband, int freq, int voltage, int tone)
{
    static struct timespec ns = { .tv_sec = 0, .tv_nsec = 15 * 1000 * 1000 };

    const int i = 4 * (mod->config.diseqc - 1)
                | 2 * hiband
                | ((voltage == SEC_VOLTAGE_18) ? 1 : 0);

    if(ioctl(mod->fe_fd, FE_SET_TONE, SEC_TONE_OFF) != 0)
    {
        mod->status.error_message = "FE_SET_TONE";
        return 0;
    }

    if(ioctl(mod->fe_fd, FE_SET_VOLTAGE, voltage) != 0)
    {
        mod->status.error_message = "FE_SET_VOLTAGE";
        return 0;
    }

    nanosleep(&ns, NULL);

    struct dvb_diseqc_master_cmd cmd =
    {
        .msg_len = 4,
        .msg = { 0xE0, 0x10, 0x38, 0xF0 | i, 0x00, 0x00 }
    };

    if(ioctl(mod->fe_fd, FE_DISEQC_SEND_MASTER_CMD, &cmd) != 0)
    {
        mod->status.error_message = "FE_DISEQC_SEND_MASTER_CMD";
        return 0;
    }

    nanosleep(&ns, NULL);

    fe_sec_mini_cmd_t burst = ((i / 4) % 2) ? SEC_MINI_B : SEC_MINI_A;
    if(ioctl(mod->fe_fd, FE_DISEQC_SEND_BURST, burst) != 0)
    {
        mod->status.error_message = "FE_DISEQC_SEND_BURST";
        return 0;
    }

    nanosleep(&ns, NULL);

    if(ioctl(mod->fe_fd, FE_SET_TONE, tone) != 0)
    {
        mod->status.error_message = "FE_SET_TONE";
        return 0;
    }

    return 1;
} /* diseqc_setup */

/* in thread function */
static int frontend_tune_s(module_data_t *mod
                           , int freq, int voltage, int tone)
{
    struct dvb_frontend_parameters feparams;

    memset(&feparams, 0, sizeof(feparams));
    feparams.frequency = freq;
    feparams.inversion = INVERSION_AUTO;
    feparams.u.qpsk.symbol_rate = mod->config.tp.srate;
    feparams.u.qpsk.fec_inner = FEC_AUTO;

    if(!mod->config.diseqc)
    {
        if(ioctl(mod->fe_fd, FE_SET_TONE, tone) != 0)
        {
            mod->status.error_message = "FE_SET_TONE";
            return 0;
        }

        if(ioctl(mod->fe_fd, FE_SET_VOLTAGE, voltage) != 0)
        {
            mod->status.error_message = "FE_SET_VOLTAGE";
            return 0;
        }
    }

    if(ioctl(mod->fe_fd, FE_SET_FRONTEND, &feparams) != 0)
    {
        mod->status.error_message = "FE_SET_FRONTEND";
        return 0;
    }

    return 1;
} /* frontend_tune_s */

/* in thread function */
static int frontend_tune_s2(module_data_t *mod
                            , int freq, int voltage, int tone)
{

    /* clear */
    struct dtv_properties cmdseq;
    int fec = FEC_AUTO;

    struct dtv_property cmd_clear[] =
    {
        { .cmd = DTV_CLEAR }
    };
    cmdseq.num = 1;
    cmdseq.props = cmd_clear;

    if(ioctl(mod->fe_fd, FE_SET_PROPERTY, &cmdseq) != 0)
    {
        mod->status.error_message = "FE_SET_PROPERTY clear";
        return 0;
    }

    /* tune */
    struct dtv_property cmd_tune[] =
    {
        { .cmd = DTV_DELIVERY_SYSTEM,   .u.data = SYS_DVBS2             },
        { .cmd = DTV_FREQUENCY,         .u.data = freq                  },
        { .cmd = DTV_SYMBOL_RATE,       .u.data = mod->config.tp.srate  },
        { .cmd = DTV_INNER_FEC,         .u.data = fec                   },
        { .cmd = DTV_INVERSION,         .u.data = INVERSION_AUTO        },
        { .cmd = DTV_VOLTAGE,           .u.data = voltage               },
        { .cmd = DTV_TONE,              .u.data = tone                  },
        { .cmd = DTV_TUNE                                               }
    };
    cmdseq.num = ARRAY_SIZE(cmd_tune);
    cmdseq.props = cmd_tune;

    struct dvb_frontend_event ev;
    while(ioctl(mod->fe_fd, FE_GET_EVENT, &ev) != -1)
        ;

    if(ioctl(mod->fe_fd, FE_SET_PROPERTY, &cmdseq) != 0)
    {
        mod->status.error_message = "FE_SET_PROPERTY tune";
        return 0;
    }

    return 1;
} /* frontend_tune_s2 */

/* in thread function */
static void frontend_tune(module_data_t *mod)
{
    int freq = mod->config.tp.freq;

    int hiband = (mod->config.lnb.slof && mod->config.lnb.lof2
                  && freq > mod->config.lnb.slof);
    if(hiband)
        freq = freq - mod->config.lnb.lof2;
    else
    {
        if(freq < mod->config.lnb.lof1)
            freq = mod->config.lnb.lof1 - freq;
        else
            freq = freq - mod->config.lnb.lof1;
    }

    int voltage = SEC_VOLTAGE_OFF;
    int tone = SEC_TONE_OFF;
    if(!mod->config.lnb_sharing)
    {
        voltage = (mod->config.tp.pol == TP_POL_V)
                ? SEC_VOLTAGE_13 : SEC_VOLTAGE_18;
        if(hiband)
            tone = SEC_TONE_ON;
    }

    int ret = 0;
    do
    {

        if(mod->config.diseqc)
        {
            ret = diseqc_setup(mod, hiband, freq, voltage, tone);
            if(!ret)
                break;
        }

        switch(mod->config.type)
        {
            case DVB_TYPE_S:
                ret = frontend_tune_s(mod, freq, voltage, tone);
                break;
            case DVB_TYPE_S2:
                ret = frontend_tune_s2(mod, freq, voltage, tone);
                break;
            default:
                mod->status.error_message = "unknown dvb type";
                ret = 0;
                break;
        }

        return;
    } while(0);

    if(!ret)
    {
        int message = MESSAGE_ERROR;
        stream_send(mod->thread_stream, &message, sizeof(message));
    }
} /* frontend_tune */

/* in thread function */
static void frontend_status(module_data_t *mod)
{
    if(ioctl(mod->fe_fd, FE_READ_STATUS, &mod->status.fe) != 0)
    {
        mod->status.error_message = "FE_READ_STATUS";
        // send message to main thread
        int message = MESSAGE_ERROR;
        stream_send(mod->thread_stream, &message, sizeof(message));
        return;
    }

    uint16_t snr, signal;
    if(ioctl(mod->fe_fd, FE_READ_SIGNAL_STRENGTH, &signal) != 0)
        signal = -2;
    if(ioctl(mod->fe_fd, FE_READ_SNR, &snr) == -1)
        snr = -2;
    if(ioctl(mod->fe_fd, FE_READ_BER, &mod->status.ber) == -1)
        mod->status.ber = -2;
    if(ioctl(mod->fe_fd, FE_READ_UNCORRECTED_BLOCKS, &mod->status.unc) == -1)
        mod->status.unc = -2;

    mod->status.lock = ((mod->status.fe & FE_HAS_LOCK) != 0);
    mod->status.signal = (signal * 100) / 0xffff;
    mod->status.snr = (snr * 100) / 0xffff;

    // send message to main thread
    int message;
    if(!mod->status.lock)
    {
        // retune
        frontend_tune(mod);
        message = MESSAGE_RETUNE;
        stream_send(mod->thread_stream, &message, sizeof(message));
        sleep(RETUNE_INTERVAL - STATUS_INTERVAL);
    }
    else
    {
        if(!mod->is_lock)
        {
            message = MESSAGE_LOCK;
            stream_send(mod->thread_stream, &message, sizeof(message));
        }
    }
} /* frontend status */

static void frontend_thread(void *arg)
{
    module_data_t *mod = arg;

    frontend_tune(mod);
    while(thread_is_started(mod->thread))
    {
        sleep(STATUS_INTERVAL);
        frontend_status(mod);
    }
} /* frontend_thread */

static void demux_bounce(module_data_t *);

static void frontend_thread_stream(void *arg)
{
    /* messages from thread */

    module_data_t *mod = arg;
    int message = 0;
    stream_recv(mod->thread_stream, &message, sizeof(message));

    switch(message)
    {
        case MESSAGE_LOCK:
        {
            log_info(LOG_MSG("lock signal:%3u%% snr:%3u%%")
                     , mod->status.signal, mod->status.snr);
            if(!mod->is_lock)
            {
                if(mod->is_retune)
                {
                    mod->is_retune = 0;
                    demux_bounce(mod);
                }
                mod->is_lock = 1;
                module_event_call(mod);
            }
            break;
        }
        case MESSAGE_ERROR:
        {
            log_error(LOG_MSG("%s failed [%s]")
                      , mod->status.error_message, strerror(errno));
            mod->is_lock = 0;
            break;
        }
        case MESSAGE_RETUNE:
        {
            log_info(LOG_MSG("retune status:%02X"), mod->status.fe);
            if(mod->is_lock)
            {
                mod->is_retune = 1;
                mod->is_lock = 0;
                module_event_call(mod);
            }
            break;
        }
        default:
            break;
    }
}

static int frontend_open(module_data_t *mod)
{
    char dev_name[32];
    sprintf(dev_name, "/dev/dvb/adapter%d/frontend%d"
            , mod->config.adapter, mod->config.device);

    mod->fe_fd = open(dev_name, O_RDWR | O_NONBLOCK);
    if(mod->fe_fd <= 0)
    {
        log_error(LOG_MSG("failed to open frontend %s [%s]")
                  , dev_name, strerror(errno));
        return 0;
    }

    /* check dvb */
    struct dvb_frontend_info feinfo;
    if(ioctl(mod->fe_fd, FE_GET_INFO, &feinfo) != 0)
        return 0;

    switch(feinfo.type)
    {
        case FE_QPSK:
        {
            if(mod->config.type == DVB_TYPE_S2
               && !(feinfo.caps & FE_CAN_2G_MODULATION))
            {
                log_error(LOG_MSG("S2 not suppoerted by DVB card"));
                return 0;
            }
            break;
        }
        default:
        {
            log_error(LOG_MSG("unknown card type"));
            return 0;
        }
    }

    mod->thread_stream = stream_init(frontend_thread_stream, mod);
    thread_init(&mod->thread, frontend_thread, mod);

    return 1;
} /* frontend_open */

static void frontend_close(module_data_t *mod)
{
    thread_destroy(&mod->thread);
    stream_destroy(mod->thread_stream);

    if(mod->fe_fd > 0)
        close(mod->fe_fd);
} /* frontend_close */

/* dvr */

static int dvr_open(module_data_t *);

static void dvr_close(module_data_t *mod)
{
    mod->bitrate = 0;
    mod->stat_count = 0;

    if(mod->dvr_fd > 0)
    {
        event_detach(mod->dvr_fd);
        close(mod->dvr_fd);
        mod->dvr_fd = 0;
    }
}

static void dvr_read_callback(void *arg, int event)
{
    module_data_t *mod = arg;

    if(event == EVENT_ERROR)
    {
        log_warning(LOG_MSG("read error [%s]. try to reopen")
                    , strerror(errno));
        dvr_close(mod);
        dvr_open(mod);
        return;
    }

    const ssize_t len = read(mod->dvr_fd, mod->buffer, BUFFER_SIZE);
    if(len <= 0)
    {
        dvr_read_callback(arg, EVENT_ERROR);
        return;
    }

    mod->stat_count += len;

    for(int i = 0; i < len; i += TS_PACKET_SIZE)
        stream_ts_send(mod, &mod->buffer[i]);
}

static void stat_timer_callback(void *arg)
{
    module_data_t *mod = arg;
    if(!mod->stat_count)
    {
        if(mod->bitrate)
        {
            mod->bitrate = 0;
            log_warning(LOG_MSG("bitrate: 0Kbit/s"));
        }
    }
    else
    {
        const int is_bitrate = (mod->bitrate > 1);
        mod->bitrate = (mod->stat_count * 8 / 1024) / (STAT_INTERVAL / 1000);
        mod->stat_count = 0;
        if(mod->bitrate)
        {
            if(!is_bitrate)
            {
                log_info(LOG_MSG("bitrate: %dKbit/s"), mod->bitrate);
            }
        }
    }
}

static void dvr_reopen_callback(void *arg)
{
    module_data_t *mod = arg;
    dvr_open(mod);
}

static int dvr_open(module_data_t *mod)
{
    char dev_name[32];
    sprintf(dev_name, "/dev/dvb/adapter%d/dvr%d"
            , mod->config.adapter, mod->config.device);
    mod->dvr_fd = open(dev_name, O_RDONLY | O_NONBLOCK);
    if(mod->dvr_fd <= 0)
    {
        log_error(LOG_MSG("failed to open dvr %s [%s]")
                  , dev_name, strerror(errno));
        if(mod->demux.dev_name[0]) // if not first time opening
        {
            log_warning(LOG_MSG("try to reopen after 5s"));
            timer_one_shot(5000, dvr_reopen_callback, mod);
        }
        return 0;
    }

    if(mod->config.buffer_size > 0)
    {
        const uint64_t buffer_size = mod->config.buffer_size * 4096;
        if(ioctl(mod->dvr_fd, DMX_SET_BUFFER_SIZE, buffer_size) < 0)
        {
            log_warning(LOG_MSG("failed to set dvr ring buffer [%s]")
                        , strerror(errno));
        }
    }

    event_attach(mod->dvr_fd, dvr_read_callback, mod, EVENT_READ);

    return 1;
}

/* demux */

static void demux_join_pid(module_data_t *mod, uint16_t pid)
{
    if(mod->config.budget)
    {
        if(pid != MAX_PID || mod->demux.fd_8192 > 0)
            return;
    }
    else
    {
        if(pid >= MAX_PID)
            return;
        ++mod->join_list[pid];
        if(mod->demux.fd_list[pid] > 0)
            return;
    }

    if(mod->demux.dev_name[0] == 0) // not yet initialized
        return;

    int fd = open(mod->demux.dev_name, O_WRONLY);
    if(fd <= 0)
    {
        log_error(LOG_MSG("failed to open demux %s [%s]")
                  , mod->demux.dev_name, strerror(errno));
        return;
    }

    struct dmx_pes_filter_params pes_filter;
    memset(&pes_filter, 0, sizeof(pes_filter));
    pes_filter.pid = pid;
    pes_filter.input = DMX_IN_FRONTEND;
    pes_filter.output = DMX_OUT_TS_TAP;
    pes_filter.pes_type = DMX_PES_OTHER;
    pes_filter.flags = DMX_IMMEDIATE_START;

    if(ioctl(fd, DMX_SET_PES_FILTER, &pes_filter) < 0)
    {
        log_error(LOG_MSG("failed to set PES filter [%s]")
                  , strerror(errno));
        close(fd);
        return;
    }

    if(pid < MAX_PID)
        mod->demux.fd_list[pid] = fd;
    else
        mod->demux.fd_8192 = fd;
} // demux_join_pid

static void demux_leave_pid(module_data_t *mod, uint16_t pid)
{
    if(mod->config.budget)
    {
        if(pid == MAX_PID && mod->demux.fd_8192 > 0)
        {
            close(mod->demux.fd_8192);
            mod->demux.fd_8192 = 0;
        }
    }
    else
    {
        --mod->join_list[pid];
        if(pid < MAX_PID
           && mod->demux.fd_list[pid] > 0
           && mod->join_list[pid] <= 0)
        {
            close(mod->demux.fd_list[pid]);
            mod->demux.fd_list[pid] = 0;
            mod->join_list[pid] = 0;
        }
    }
} // demux_leave_pid

static void demux_bounce(module_data_t *mod)
{
    if(mod->config.budget)
    {
        if(mod->demux.fd_8192 > 0)
        {
            ioctl(mod->demux.fd_8192, DMX_STOP);
            ioctl(mod->demux.fd_8192, DMX_START);
        }
    }
    else
    {
        for(int i = 0; i < MAX_PID; ++i)
        {
            const int fd = mod->demux.fd_list[i];
            if(fd <= 0)
                continue;
            ioctl(fd, DMX_STOP);
            ioctl(fd, DMX_START);
        }
    }
} /* demux_bounce */

/* stream_ts callbacks */

static void callback_join_pid(module_data_t *mod
                              , module_data_t *child
                              , uint16_t pid)
{
    demux_join_pid(mod, pid);
}

static void callback_leave_pid(module_data_t *mod
                               , module_data_t *child
                               , uint16_t pid)
{
    demux_leave_pid(mod, pid);
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

static int method_status(module_data_t *mod)
{
    lua_State *L = LUA_STATE(mod);

    lua_newtable(L);
    lua_pushnumber(L, mod->status.fe);
    lua_setfield(L, -2, "status");
    lua_pushboolean(L, mod->status.lock);
    lua_setfield(L, -2, "lock");
    lua_pushnumber(L, mod->status.signal);
    lua_setfield(L, -2, "signal");
    lua_pushnumber(L, mod->status.snr);
    lua_setfield(L, -2, "snr");
    lua_pushnumber(L, mod->status.ber);
    lua_setfield(L, -2, "ber");
    lua_pushnumber(L, mod->status.unc);
    lua_setfield(L, -2, "unc");
    lua_pushnumber(L, mod->bitrate);
    lua_setfield(L, -2, "bitrate");

    return 1;
}

static int method_event(module_data_t *mod)
{
    module_event_set(mod);
    return 0;
}

/* public */

static void module_init(module_data_t *mod)
{
    log_debug(LOG_MSG("init"));

    /* set dvb type */
    if(mod->config._type[0] == 's' || mod->config._type[0] == 'S')
    {
        if(mod->config._type[1] == '2')
            mod->config.type = DVB_TYPE_S2;
        else
            mod->config.type = DVB_TYPE_S;

        /* set TP */
        int is_tp_ok = 0;
        do
        {
            char *conf = mod->config._tp;
            // set frequency
            mod->config.tp.freq = atoi(conf) * 1000;
            for(; *conf && *conf != ':'; ++conf)
                ;
            if(!*conf)
                break;
            ++conf;
            // set polarization
            char pol = *conf;
            if(pol > 'Z')
                pol -= ('z' - 'Z'); // Upper Case
            // V - vertical/right // H - horizontal/left
            mod->config.tp.pol = (pol == 'V' || pol == 'R') ? TP_POL_V : TP_POL_H;
            for(; *conf && *conf != ':'; ++conf)
                ;
            if(!*conf)
                break;
            ++conf;
            // set symbol-rate
            mod->config.tp.srate = atoi(conf) * 1000;
            is_tp_ok = 1;
        } while(0);
        if(!is_tp_ok)
        {
            log_error(LOG_MSG("failed to set transponder \"%s\"")
                      , mod->config._tp);
            return;
        }

        /* set LNB */
        int is_lnb_ok = 0;
        do
        {
            char *conf = mod->config._lnb;
            // set lof1 (low)
            mod->config.lnb.lof1 = atoi(conf) * 1000;
            for(; *conf && *conf != ':'; ++conf)
                ;
            if(!*conf)
            {
                if(mod->config.lnb.lof1)
                {
                    mod->config.lnb.lof2 = mod->config.lnb.lof1;
                    mod->config.lnb.slof = mod->config.lnb.lof1;
                    is_lnb_ok = 1;
                }
                break;
            }
            ++conf;
            // set lof2 (high)
            mod->config.lnb.lof2 = atoi(conf) * 1000;
            for(; *conf && *conf != ':'; ++conf)
                ;
            if(!*conf)
                break;
            ++conf;
            // set slof (switch)
            mod->config.lnb.slof = atoi(conf) * 1000;
            is_lnb_ok = 1;
        } while(0);
        if(!is_lnb_ok)
        {
            log_error(LOG_MSG("failed to set LNB \"%s\"")
                      , mod->config._lnb);
            return;
        }
    }
    else
    {
        log_error(LOG_MSG("unknown DVB type \"%s\""), mod->config._type);
        return;
    }

    /* protocols */
    stream_ts_init(mod, NULL, NULL, NULL
                   , callback_join_pid, callback_leave_pid);

    if(mod->config._tp)
        frontend_open(mod);

    dvr_open(mod);

    sprintf(mod->demux.dev_name, "/dev/dvb/adapter%d/demux%d"
            , mod->config.adapter, mod->config.device);

    if(mod->config.budget)
        demux_join_pid(mod, MAX_PID);

    for(int i = 0; i < MAX_PID; i++)
    {
        if(mod->join_list[i] <= 0)
            continue;
        --mod->join_list[i]; // cheat to prevent double count
        demux_join_pid(mod, i);
    }

    mod->bitrate = 1; // to detect error after module has started
    mod->stat_timer = timer_attach(STAT_INTERVAL, stat_timer_callback, mod);
}

static void module_destroy(module_data_t *mod)
{
    log_debug(LOG_MSG("destroy"));

    /* protocols */
    stream_ts_destroy(mod);
    module_event_destroy(mod);

    timer_detach(mod->stat_timer);

    if(mod->config._tp)
        frontend_close(mod);

    if(mod->config.budget)
        demux_leave_pid(mod, MAX_PID);
    else
    {
        for(int i = 0; i < MAX_PID; i++)
        {
            if(mod->join_list[i] > 1) // cheat to close descriptor forcibly
                mod->join_list[i] = 1;
            demux_leave_pid(mod, i);
        }
    }

    dvr_close(mod);
}

MODULE_OPTIONS()
{
    OPTION_NUMBER("adapter"    , config.adapter    , 1, 0)
    OPTION_NUMBER("device"     , config.device     , 0, 0)
    OPTION_NUMBER("diseqc"     , config.diseqc     , 0, 0)
    OPTION_STRING("type"       , config._type      , 1, NULL)
    OPTION_NUMBER("lnb_sharing", config.lnb_sharing, 0, 0)
    OPTION_STRING("lnb"        , config._lnb       , 0, NULL)
    OPTION_STRING("tp"         , config._tp        , 0, NULL)
    OPTION_NUMBER("budget"     , config.budget     , 0, 0)
    OPTION_NUMBER("buffer_size", config.buffer_size, 0, 0)
};

MODULE_METHODS()
{
    METHOD(status)
    METHOD(attach)
    METHOD(detach)
    METHOD(event)
};

MODULE(dvb_input)

#endif
