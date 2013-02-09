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

#include <sys/queue.h>

#include "lua/lua.h"
#include "lua/lualib.h"
#include "lua/lauxlib.h"

extern lua_State *lua; // in main.c

#define STACK_DEBUG(_L, _pos)                                               \
    printf("%s(): stack %d: %d\n", __FUNCTION__, _pos, lua_gettop(_L))

#include "version.h"
#define __VSTR(_x) #_x
#define _VSTR(_x) __VSTR(_x)
#define _VERSION "v." _VSTR(ASTRA_VERSION) "." _VSTR(ASTRA_VERSION_DEV)
#ifdef DEBUG
#   define _VDEBUG " debug"
#else
#   define _VDEBUG
#endif
#define ASTRA_VERSION_STR _VERSION _VDEBUG

typedef struct module_data_s module_data_t;

/* module_stream_* */

typedef struct
{
    void (*on_ts)(module_data_t *mod, const uint8_t *ts);
} module_stream_api_t;

#define MODULE_STREAM_BASE()                                                \
    struct                                                                  \
    {                                                                       \
        module_stream_api_t api;                                            \
        module_data_t *parent;                                              \
        TAILQ_ENTRY(module_data_s) entries;                                 \
        TAILQ_HEAD(list_s, module_data_s) childs;                           \
    } __stream

#define MODULE_STREAM_METHODS()                                             \
    { "attach", module_stream_attach },                                     \
    { "detach", module_stream_detach }

int module_stream_attach(module_data_t *mod);
int module_stream_detach(module_data_t *mod);

void module_stream_send(module_data_t *mod, const uint8_t *ts);

void module_stream_init(module_data_t *mod, module_stream_api_t *api);
void module_stream_destroy(module_data_t *mod);

/* module_lua_* */

typedef struct
{
    const char *name;
    int (*method)(module_data_t *mod);
} module_method_t;

#define MODULE_METHODS()                                                    \
    static const module_method_t __module_methods[] =
#define METHOD(_name) { #_name, method_##_name },
#define MODULE_METHODS_EMPTY()                                              \
    MODULE_METHODS() {{ NULL, NULL }}

#define MODULE_LUA_REGISTER(_name)                                          \
    static const char __module_name[] = #_name;                             \
    static int __module_new(lua_State *L)                                   \
    {                                                                       \
        module_data_t *mod = lua_newuserdata(L, sizeof(module_data_t));     \
        memset(mod, 0, sizeof(module_data_t));                              \
        lua_getmetatable(L, 1);                                             \
        lua_setmetatable(L, -2);                                            \
        module_initialize(mod);                                             \
        return 1;                                                           \
    }                                                                       \
    static int __module_delete(lua_State *L)                                \
    {                                                                       \
        module_data_t *mod = luaL_checkudata(L, 1, __module_name);          \
        module_destroy(mod);                                                \
        return 0;                                                           \
    }                                                                       \
    static int __module_thunk(lua_State *L)                                 \
    {                                                                       \
        module_data_t *mod = luaL_checkudata(L, 1, __module_name);          \
        module_method_t *m = lua_touserdata(L, lua_upvalueindex(1));        \
        return m->func(mod);                                                \
    }                                                                       \
    static int __module_tostring(lua_State *L)                              \
    {                                                                       \
        lua_pushstring(L, __module_name);                                   \
        return 1;                                                           \
    }                                                                       \
    LUA_API int luaopen_##_name(lua_State *L)                               \
    {                                                                       \
        static const luaL_Reg meta_methods[] =                              \
        {                                                                   \
            { "__gc", __module_delete },                                    \
            { "__tostring", __module_tostring },                            \
            { "__call", __module_new },                                     \
            { NULL, NULL }                                                  \
        };                                                                  \
        lua_newtable(L);                                                    \
        const int module_table = lua_gettop(L);                             \
        luaL_newmetatable(L, __module_name);                                \
        const int meta_table = lua_gettop(L);                               \
        luaL_setfuncs(L, meta_methods, 0);                                  \
        lua_pushvalue(L, module_table);                                     \
        lua_setfield(L, meta_table, "__index");                             \
        luaL_newlib(L, meta_methods);                                       \
        lua_setfield(L, meta_table, "__metatable");                         \
        lua_setmetatable(L, module_table);                                  \
        if(__module_methods[0].name)                                        \
        {                                                                   \
            for(size_t i = 0; i < ARRAY_SIZE(__module_methods); ++i)        \
            {                                                               \
                const module_method_t *m = &__module_methods[i];            \
                lua_pushstring(L, m->name);                                 \
                lua_pushlightuserdata(L, (void*)m);                         \
                lua_pushcclosure(L, __module_thunk, 1);                     \
                lua_settable(L, module_table);                              \
            }                                                               \
        }                                                                   \
        lua_setglobal(L, __module_name);                                    \
        return 1;                                                           \
    }

int module_set_number(module_data_t *mod, const char *name, int *number);
int module_set_string(module_data_t *mod, const char *name, const char **string, int *length);

void astra_exit(void);
void astra_abort(void);

void astra_do_file(int argc, const char **argv, const char *fail);
void astra_do_text(int argc, const char **argv, const char *text, size_t size);

#endif /* _ASTRA_H_ */
