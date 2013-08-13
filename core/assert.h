/*
 * Astra Core
 * http://cesbo.com/en/astra
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
