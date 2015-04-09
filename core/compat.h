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

#ifndef _ASC_COMPAT_H_
#define _ASC_COMPAT_H_ 1

#include "base.h"

#ifndef __PRI64_PREFIX
#   if __WORDSIZE == 64 && !defined(__llvm__)
#       define __PRI64_PREFIX "l"
#   else
#       define __PRI64_PREFIX "ll"
#   endif
#endif

#ifndef PRId64
#   define PRId64 __PRI64_PREFIX "d"
#endif

#ifndef PRIu64
#   define PRIu64 __PRI64_PREFIX "u"
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

#ifndef EWOULDBLOCK
#   define EWOULDBLOCK EAGAIN
#endif

#ifndef HAVE_PREAD
ssize_t pread(int fd, void *buffer, size_t size, off_t off);
#endif

#ifndef HAVE_STRNDUP
char * strndup(const char *str, size_t max);
#endif

#ifndef HAVE_STRNLEN
size_t strnlen(const char *str, size_t max);
#endif

#endif /* _ASC_COMPAT_H_ */
