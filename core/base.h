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

#ifndef _BASE_H_
#define _BASE_H_ 1

#ifdef _WIN32
#   ifndef _WIN32_WINNT
#       define _WIN32_WINNT 0x0501
#   endif
#   include <winsock2.h>
#   include <windows.h>
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <setjmp.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define ASC_ARRAY_SIZE(_a) (sizeof(_a)/sizeof(_a[0]))

#define ASC_FREE(_o, _m) if(_o != NULL) { _m(_o); _o = NULL; }

#define __uarg(_x) {(void)_x;}

#ifndef __wur
#   define __wur __attribute__(( __warn_unused_result__ ))
#endif

#ifndef O_BINARY
#   ifdef _O_BINARY
#       define O_BINARY _O_BINARY
#   else
#       define O_BINARY 0
#   endif
#endif

#ifndef __BYTE_ORDER__
#   ifdef HAVE_ENDIAN_H
#       include <endian.h>
#       define __BYTE_ORDER__ __BYTE_ORDER
#       define __ORDER_LITTLE_ENDIAN__ __LITTLE_ENDIAN
#       define __ORDER_BIG_ENDIAN__ __BIG_ENDIAN
#   else
#       define __BYTE_ORDER__ 1234
#       define __ORDER_LITTLE_ENDIAN__ 1234
#       define __ORDER_BIG_ENDIAN__ 4321
#   endif
#endif

#endif /* _BASE_H_ */
