/*
 * Astra Core
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

#include "utils.h"

int64_t asc_utime(void)
{
#ifdef HAVE_CLOCK_GETTIME
    struct timespec ts;

    if(clock_gettime(CLOCK_MONOTONIC, &ts) == EINVAL)
        (void)clock_gettime(CLOCK_REALTIME, &ts);

    return ((int64_t)ts.tv_sec * 1000000) + (int64_t)(ts.tv_nsec / 1000);
#else
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return ((int64_t)tv.tv_sec * 1000000) + (int64_t)tv.tv_usec;
#endif
}
