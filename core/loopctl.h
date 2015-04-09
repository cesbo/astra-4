/*
 * Astra Core (Main loop control)
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

#ifndef _ASC_LOOPCTL_H_
#define _ASC_LOOPCTL_H_ 1

#include "base.h"

extern jmp_buf main_loop;
extern bool is_main_loop_idle;
extern lua_State *lua;

void astra_exit(void) __noreturn;
void astra_abort(void) __noreturn;
void astra_reload(void) __noreturn;

#endif /* _ASC_LOOPCTL_H_ */
