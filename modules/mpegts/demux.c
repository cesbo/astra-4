/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#include <astra.h>
#include "mpegts.h"

#define LOG_MSG(_msg) "[demux] " _msg

struct module_data_s
{
    MODULE_BASE();

    list_t *pid_attach[MAX_PID]; // pass ts packet w/o processing
};

/* stream_ts callbacks */

static void callback_send_ts(module_data_t *mod, uint8_t *ts)
{
    if(ts[0] != 0x47)
        return;

    const uint16_t pid = TS_PID(ts);

    list_t *i = list_get_first(mod->pid_attach[pid]);
    while(i)
    {
        module_data_t *dst = list_get_data(i);
        i = list_get_next(i);
        stream_ts_sendto(dst, ts);
    }
}

static void callback_on_attach(module_data_t *mod, module_data_t *parent)
{
}

static void callback_on_detach(module_data_t *mod, module_data_t *parent)
{
}

static void callback_join_pid(module_data_t *mod, module_data_t *child
                              , uint16_t pid)
{
    mod->pid_attach[pid] = list_insert(mod->pid_attach[pid], child);
    stream_ts_join_pid(mod, pid); // send to parent
}

static void callback_leave_pid(module_data_t *mod, module_data_t *child
                               , uint16_t pid)
{
    mod->pid_attach[pid] = list_delete(mod->pid_attach[pid], child);
    stream_ts_leave_pid(mod, pid); // send to parent
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

static void module_initialize(module_data_t *mod)
{
    stream_ts_init(mod, callback_send_ts
                   , callback_on_attach, callback_on_detach
                   , callback_join_pid, callback_leave_pid);
}

static void module_destroy(module_data_t *mod)
{
    stream_ts_destroy(mod);
}

MODULE_METHODS()
{
    METHOD(attach)
    METHOD(detach)
};

MODULE(demux)
