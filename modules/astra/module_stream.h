/*
 * Astra Module Stream API
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#ifndef _MODULE_STREAM_H_
#define _MODULE_STREAM_H_ 1

#include "base.h"
#include <sys/queue.h>

typedef struct
{
    void (*on_ts)(module_data_t *mod, const uint8_t *ts);
} module_stream_api_t;

#define MODULE_STREAM_DATA()                                                \
    struct                                                                  \
    {                                                                       \
        module_stream_api_t api;                                            \
        module_data_t *parent;                                              \
        TAILQ_ENTRY(module_data_s) entries;                                 \
        TAILQ_HEAD(list_s, module_data_s) childs;                           \
    } __stream

#define MODULE_STREAM_METHODS()                                             \
    { "attach", module_stream_attach },                                     \
    { "detach", module_stream_detach }

int module_stream_attach(module_data_t *mod);
int module_stream_detach(module_data_t *mod);

void module_stream_send(module_data_t *mod, const uint8_t *ts);

void module_stream_init(module_data_t *mod, module_stream_api_t *api);
void module_stream_destroy(module_data_t *mod);

int module_option_number(const char *name, int *number);
int module_option_string(const char *name, const char **string);

#endif /* _MODULE_STREAM_H_ */
