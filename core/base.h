/*
 * Astra Core
 * http://cesbo.com
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#ifndef _BASE_H_
#define _BASE_H_ 1

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <setjmp.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define __asc_assert(_cond, _file, _line, _msg...)                                              \
    ( (void)printf("%s:%u: failed assertion `%s'\n", _file, _line, _cond)                       \
    , (void)printf(_msg)                                                                        \
    , putchar('\n')                                                                             \
    , abort() )
#define asc_assert(_cond, _msg...)                                                              \
    ((void)((_condition) ? 0 : __asc_assert(#_cond, __FILE__, __LINE__, _msg)))

#define ASC_ARRAY_SIZE(_a) (sizeof(_a)/sizeof(_a[0]))

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

#endif /* _BASE_H_ */
