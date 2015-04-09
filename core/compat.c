/*
 * Astra Core (Compatibility library)
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2015, Andrey Dyldin <and@cesbo.com>
 *                    2015, Artem Kharitonov <artem@sysert.ru>
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

#include "compat.h"

#ifndef HAVE_PREAD
ssize_t pread(int fd, void *buffer, size_t size, off_t off)
{
    if(lseek(fd, off, SEEK_SET) != off)
        return -1;

    return read(fd, buffer, size);
}
#endif

#ifndef HAVE_STRNDUP
char *strndup(const char *str, size_t max)
{
    size_t len = strnlen(str, max);
    char *res = malloc(len + 1);
    if (res)
    {
        memcpy(res, str, len);
        res[len] = '\0';
    }
    return res;
}
#endif

#ifndef HAVE_STRNLEN
size_t strnlen(const char *str, size_t max)
{
    const char *end = memchr(str, 0, max);
    return (end != NULL) ? (size_t)(end - str) : max;
}
#endif
