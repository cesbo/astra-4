/*
 * Astra Module: Stream API
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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
