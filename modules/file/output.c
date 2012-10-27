/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#include <astra.h>

#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>

#define TS_PACKET_SIZE 188

#define TIMESTAMP_SIZE 4
#define M2TS_PACKET_SIZE (TS_PACKET_SIZE + TIMESTAMP_SIZE)

#define FILE_BUFFER_SIZE 0xFFFF

#define LOG_MSG(_msg) "[file_output %s] " _msg, mod->config.filename

struct module_data_s
{
    MODULE_BASE();

    struct
    {
        char *filename;
        int m2ts;
    } config;

    int fd;

    size_t file_size;

    size_t buffer_skip;
    uint8_t buffer[FILE_BUFFER_SIZE]; // write buffer
};

/* stream_ts callbacks */

static void callback_send_ts_188(module_data_t *mod, uint8_t *ts)
{
    if(mod->buffer_skip > FILE_BUFFER_SIZE - TS_PACKET_SIZE)
    {
        if(mod->buffer_skip == 0)
            return;
        if(write(mod->fd, mod->buffer, mod->buffer_skip)
           != mod->buffer_skip)
        {
            // TODO: check error
        }
        mod->file_size += mod->buffer_skip;
        mod->buffer_skip = 0;
    }
    memcpy(&mod->buffer[mod->buffer_skip], ts, TS_PACKET_SIZE);
    mod->buffer_skip += TS_PACKET_SIZE;
}

static void callback_send_ts_192(module_data_t *mod, uint8_t *ts)
{
    if(mod->buffer_skip > FILE_BUFFER_SIZE - M2TS_PACKET_SIZE)
    {
        if(mod->buffer_skip == 0)
            return;
        if(write(mod->fd, mod->buffer, mod->buffer_skip)
           != mod->buffer_skip)
        {
            // TODO: check error
        }
        mod->file_size += mod->buffer_skip;
        mod->buffer_skip = 0;
    }
    uint8_t *dst = &mod->buffer[mod->buffer_skip];
    memcpy(&dst[TIMESTAMP_SIZE], ts, TS_PACKET_SIZE);
    mod->buffer_skip += M2TS_PACKET_SIZE;

    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint32_t t = (tv.tv_usec / 1000) + (tv.tv_sec * 1000);
    dst[0] = (t >> 24) & 0xFF;
    dst[1] = (t >> 16) & 0xFF;
    dst[2] = (t >>  8) & 0xFF;
    dst[3] = (t      ) & 0xFF;
}

/* methods */

static int method_status(module_data_t *mod)
{
    lua_State *L = LUA_STATE(mod);
    lua_newtable(L);

    lua_pushnumber(L, mod->file_size);
    lua_setfield(L, -2, "size");

    return 1;
}

/* required */

static void module_init(module_data_t *mod)
{
    log_debug(LOG_MSG("init"));

    if(mod->config.m2ts)
        stream_ts_init(mod, callback_send_ts_192, NULL, NULL, NULL, NULL);
    else
        stream_ts_init(mod, callback_send_ts_188, NULL, NULL, NULL, NULL);

    if(!mod->config.filename)
        return;
    mod->fd = open(mod->config.filename, O_CREAT | O_APPEND | O_RDWR
#ifndef _WIN32
                   , S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
#else
                   , S_IRUSR | S_IWUSR);
#endif
    if(mod->fd < 0)
        log_error(LOG_MSG("can't open file [%s]"), strerror(errno));
}

static void module_destroy(module_data_t *mod)
{
    log_debug(LOG_MSG("destroy"));

    stream_ts_destroy(mod);

    if(mod->fd > 0)
        close(mod->fd);
}

MODULE_OPTIONS()
{
    OPTION_STRING("filename", config.filename, 1, NULL)
    OPTION_NUMBER("m2ts"    , config.m2ts    , 0, 0)
};

MODULE_METHODS()
{
    METHOD(status)
};

MODULE(file_output)
