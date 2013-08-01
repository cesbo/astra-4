/*
 * Astra Module Stream API
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#include <astra.h>

void __module_stream_detach(module_stream_t *stream, module_stream_t *child)
{
    module_stream_t *i, *n;
    TAILQ_FOREACH_SAFE(i, &stream->childs, entries, n)
    {
        if(i == child)
        {
            TAILQ_REMOVE(&stream->childs, i, entries);
            break;
        }
    }
    child->parent = NULL;
}

void __module_stream_attach(module_stream_t *stream, module_stream_t *child)
{
    if(child->parent)
        __module_stream_detach(child->parent, child);
    child->parent = stream;
    TAILQ_INSERT_TAIL(&stream->childs, child, entries);
}

void __module_stream_send(module_stream_t *stream, const uint8_t *ts)
{
    module_stream_t *i;
    TAILQ_FOREACH(i, &stream->childs, entries)
    {
        if(i->on_ts)
            i->on_ts(i->self, ts);
    }
}

void __module_stream_init(module_stream_t *stream)
{
    TAILQ_INIT(&stream->childs);
}

void __module_stream_destroy(module_stream_t *stream)
{
    if(stream->parent)
        __module_stream_detach(stream->parent, stream);
    module_stream_t *i, *n;
    TAILQ_FOREACH_SAFE(i, &stream->childs, entries, n)
    {
        i->parent = NULL;
        TAILQ_REMOVE(&stream->childs, i, entries);
    }
}
