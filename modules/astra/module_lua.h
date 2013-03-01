/*
 * Astra Module Lua API
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#ifndef _MODULE_LUA_H_
#define _MODULE_LUA_H_ 1

#include "base.h"

typedef struct
{
    const char *name;
    int (*method)(module_data_t *mod);
} module_lua_method_t;

#define MODULE_OPTIONS_IDX 2

#define MODULE_LUA_METHODS()                                                \
    static const module_lua_method_t __module_lua_methods[] =

#define MODULE_LUA_REGISTER(_name)                                          \
    static const char __module_name[] = #_name;                             \
    static int __module_new(lua_State *L)                                   \
    {                                                                       \
        module_data_t *mod = lua_newuserdata(L, sizeof(module_data_t));     \
        memset(mod, 0, sizeof(module_data_t));                              \
        lua_getmetatable(L, 1);                                             \
        lua_setmetatable(L, -2);                                            \
        module_init(mod);                                                   \
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
        module_lua_method_t *m = lua_touserdata(L, lua_upvalueindex(1));    \
        return m->method(mod);                                              \
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
        if(__module_lua_methods[0].name)                                    \
        {                                                                   \
            for(size_t i = 0; i < ARRAY_SIZE(__module_lua_methods); ++i)    \
            {                                                               \
                const module_lua_method_t *m = &__module_lua_methods[i];    \
                lua_pushstring(L, m->name);                                 \
                lua_pushlightuserdata(L, (void*)m);                         \
                lua_pushcclosure(L, __module_thunk, 1);                     \
                lua_settable(L, module_table);                              \
            }                                                               \
        }                                                                   \
        lua_setglobal(L, __module_name);                                    \
        return 1;                                                           \
    }

int module_option_number(const char *name, int *number);
int module_option_string(const char *name, const char **string);

#endif /* _MODULE_LUA_H_ */
