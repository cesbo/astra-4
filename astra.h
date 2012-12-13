/*
 * AsC Framework
 * http://cesbo.com
 *
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#ifndef _ASTRA_H_
#define _ASTRA_H_ 1

#include "asc/asc.h"
#include "lua/lua.h"
#include "lua/lualib.h"
#include "lua/lauxlib.h"

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

#define STACK_DEBUG(_L, _pos)                                               \
    printf("%s(): stack %d: %d\n", __FUNCTION__, _pos, lua_gettop(_L))

typedef struct module_data_s module_data_t;
typedef struct module_option_s module_option_t;

#include "modules/protocols.h"

/* ------- */

/* modules */

typedef void (*interface_t)(module_data_t *, ...);

#define MODULE_INTERFACE(_id, _callback)                                    \
    mod->__interface[_id] = (interface_t)_callback

#define MODULE_BASE()                                                       \
    lua_State *__L;                                                         \
    const char *__name;                                                     \
    int __idx_options;                                                      \
    interface_t __interface[16];                                            \
    struct { ASTRA_PROTOCOLS } __protocols

#define LUA_STATE(_mod) _mod->__L

typedef struct
{
    const char *name;
    int (*func)(module_data_t *);
} module_method_t;

#define MODULE_METHODS()                                                    \
    static const module_method_t __module_methods[] =
#define METHOD(_name) { #_name, method_##_name },
#define MODULE_METHODS_EMPTY()                                              \
    MODULE_METHODS() {{ NULL, NULL }}

#define MODULE(_name)                                                       \
    static const char __module_name[] = #_name;                             \
    static int __module_new(lua_State *L)                                   \
    {                                                                       \
        module_data_t *mod = lua_newuserdata(L, sizeof(module_data_t));     \
        memset(mod, 0, sizeof(module_data_t));                              \
        mod->__L = L;                                                       \
        mod->__name = __module_name;                                        \
        lua_getmetatable(L, 1);                                             \
        lua_setmetatable(L, -2);                                            \
        module_initialize(mod);                                             \
        if(lua_type(L, 2) == LUA_TTABLE)                                    \
        {                                                                   \
            lua_pushvalue(L, 2);                                            \
            mod->__idx_options = luaL_ref(L, LUA_REGISTRYINDEX);            \
        }                                                                   \
        return 1;                                                           \
    }                                                                       \
    static int __module_delete(lua_State *L)                                \
    {                                                                       \
        module_data_t *mod = luaL_checkudata(L, 1, __module_name);          \
        if(mod->__idx_options)                                              \
        {                                                                   \
            luaL_unref(L, LUA_REGISTRYINDEX, mod->__idx_options);           \
            mod->__idx_options = 0;                                         \
        }                                                                   \
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

ASC_API int module_set_number(module_data_t *, const char *, int
                              , int, int *);
ASC_API int module_set_string(module_data_t *, const char *, int
                              , const char *, const char **);

void astra_do_file(int, const char **, const char *);
void astra_do_text(int, const char **, const char *, size_t);

#define CHECK_RET(_cond, _ret)                                              \
    if(_cond)                                                               \
    {                                                                       \
        log_error("%s:%d %s(): " #_cond, __FILE__, __LINE__, __FUNCTION__); \
        _ret;                                                               \
    }

#endif /* _ASTRA_H_ */
