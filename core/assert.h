/*
 * Astra Core
 * http://cesbo.com
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#ifndef _ASSERT_H_
#define _ASSERT_H_ 1

#include "log.h"

#ifndef _ASTRA_H_
#   define astra_abort() abort()
#endif

#define __asc_assert(_cond, _file, _line, ...)                                                  \
    ( asc_log_error("%s:%u: failed assertion `%s'", _file, _line, _cond)                  \
    , asc_log_error(__VA_ARGS__)                                                          \
    , astra_abort() )
#define asc_assert(_cond, ...)                                                                  \
    ((_cond) ? (void)0 : __asc_assert(#_cond, __FILE__, __LINE__, __VA_ARGS__))

#endif /* _ASSERT_H_ */
