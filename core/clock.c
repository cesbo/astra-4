/*
 * Astra Core (Clock)
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

#include "clock.h"

__asc_inline
uint64_t asc_utime(void)
{
#ifdef HAVE_CLOCK_GETTIME
    struct timespec ts;

    if(clock_gettime(CLOCK_MONOTONIC, &ts) == EINVAL)
        (void)clock_gettime(CLOCK_REALTIME, &ts);

    return ((uint64_t)ts.tv_sec * 1000000) + (uint64_t)(ts.tv_nsec / 1000);
#else
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return ((uint64_t)tv.tv_sec * 1000000) + (uint64_t)tv.tv_usec;
#endif
}

__asc_inline
void asc_usleep(uint64_t usec)
{
#ifndef _WIN32
    struct timespec ts;
    ts.tv_sec = usec / 1000000;
    ts.tv_nsec = (usec % 1000000) * 1000;
    while(nanosleep(&ts, &ts) == -1 && errno == EINTR)
         continue;
#else
    HANDLE timer;
    LARGE_INTEGER ft;
    ft.QuadPart = -(usec * 10);
    timer = CreateWaitableTimer(NULL, TRUE, NULL);
    SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
    WaitForSingleObject(timer, INFINITE);
    CloseHandle(timer);
#endif
}
