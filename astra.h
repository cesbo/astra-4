/*
 * Astra
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#ifndef _ASTRA_H_
#define _ASTRA_H_ 1

#include "core/asc.h"

#include "lua/lua.h"
#include "lua/lualib.h"
#include "lua/lauxlib.h"

#include "modules/astra/base.h"
#include "modules/astra/module_lua.h"
#include "modules/astra/module_stream.h"
#include "modules/astra/module_event.h"

#include "modules/mpegts/mpegts.h"
#include "modules/mpegts/module_demux.h"

extern lua_State *lua; // in main.c
#define STACK_DEBUG(_pos) printf("%s(): stack %d: %d\n", __FUNCTION__, _pos, lua_gettop(lua))

/* version */

#include "version.h"
#define __VSTR(_x) #_x
#define _VSTR(_x) __VSTR(_x)
#define _VERSION _VSTR(ASTRA_VERSION_MAJOR) "." _VSTR(ASTRA_VERSION_MINOR)

#if ASTRA_VERSION_DEV > 0
#   define _VDEV " dev:" _VSTR(ASTRA_VERSION_DEV)
#else
#   define _VDEV
#endif

#ifdef DEBUG
#   define _VDEBUG " debug"
#else
#   define _VDEBUG
#endif

#define ASTRA_VERSION_STR _VERSION _VDEV _VDEBUG

/* main app */

void astra_exit(void);
void astra_abort(void);

void astra_do_file(int argc, const char **argv, const char *fail);
void astra_do_text(int argc, const char **argv, const char *text, size_t size);

#endif /* _ASTRA_H_ */
