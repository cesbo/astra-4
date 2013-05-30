/*
 * Astra File Module
 * http://cesbo.com/
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

/*
 * Module Name:
 *      file_output
 *
 * Module Options:
 *      filename    - string, output file name
 *      m2ts        - boolean, use m2ts file format
 *      buffer_size - number, output buffer size. in kilobytes. by default: 32
 *
 * Module Methods:
 *      status      - return table with items:
 *                    size      - number, current file size
 */

#include <astra.h>

#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>

#define FILE_BUFFER_SIZE 32

#define MSG(_msg) "[file_output %s] " _msg, mod->filename

struct module_data_t
{
    MODULE_LUA_DATA();
    MODULE_STREAM_DATA();

    const char *filename;
    int m2ts;

    int fd;

    size_t file_size;

    ssize_t buffer_size;
    ssize_t buffer_skip;
    uint8_t *buffer; // write buffer
};

/* stream_ts callbacks */

static void on_ts_188(module_data_t *mod, const uint8_t *ts)
{
    if(mod->buffer_skip > mod->buffer_size - TS_PACKET_SIZE)
    {
        if(mod->buffer_skip == 0)
            return;
        if(write(mod->fd, mod->buffer, mod->buffer_skip) != mod->buffer_skip)
        {
            // TODO: check error
        }
        mod->file_size += mod->buffer_skip;
        mod->buffer_skip = 0;
    }
    memcpy(&mod->buffer[mod->buffer_skip], ts, TS_PACKET_SIZE);
    mod->buffer_skip += TS_PACKET_SIZE;
}

static void on_ts_192(module_data_t *mod, const uint8_t *ts)
{
    if(mod->buffer_skip > mod->buffer_size - M2TS_PACKET_SIZE)
    {
        if(mod->buffer_skip == 0)
            return;
        if(write(mod->fd, mod->buffer, mod->buffer_skip) != mod->buffer_skip)
        {
            // TODO: check error
        }
        mod->file_size += mod->buffer_skip;
        mod->buffer_skip = 0;
    }
    uint8_t *dst = &mod->buffer[mod->buffer_skip];
    memcpy(&dst[4], ts, TS_PACKET_SIZE);
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
    lua_newtable(lua);

    lua_pushnumber(lua, mod->file_size);
    lua_setfield(lua, -2, "size");

    return 1;
}

/* required */

static void module_init(module_data_t *mod)
{
    module_option_string("filename", &mod->filename);
    if(!mod->filename)
    {
        asc_log_error("[file_output] option 'filename' is required");
        astra_abort();
    }

    module_option_number("m2ts", &mod->m2ts);

    int buffer_size = 0;
    if(!module_option_number("buffer_size", &buffer_size))
        mod->buffer_size = FILE_BUFFER_SIZE * 1024;
    else
        mod->buffer_size = buffer_size * 1024;

    mod->buffer = malloc(mod->buffer_size);

    if(mod->m2ts)
    {
        module_stream_init(mod, on_ts_192);
    }
    else
    {
        module_stream_init(mod, on_ts_188);
    }

    mod->fd = open(mod->filename, O_CREAT | O_APPEND | O_RDWR
#ifndef _WIN32
                   | O_BINARY
                   , S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
#else
                   , S_IRUSR | S_IWUSR);
#endif

    struct stat st;
    fstat(mod->fd, &st);
    mod->file_size = st.st_size;

    if(mod->fd <= 0)
    {
        asc_log_error(MSG("failed to open file [%s]"), strerror(errno));
        astra_abort();
    }
}

static void module_destroy(module_data_t *mod)
{
    module_stream_destroy(mod);

    if(mod->fd > 0)
        close(mod->fd);

    free(mod->buffer);
}

MODULE_LUA_METHODS()
{
    { "status", method_status }
};

MODULE_LUA_REGISTER(file_output)
