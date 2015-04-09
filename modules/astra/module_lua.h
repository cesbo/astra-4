/*
 * Astra Module: Lua API
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2015, Andrey Dyldin <and@cesbo.com>
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

#ifndef _MODULE_LUA_H_
#define _MODULE_LUA_H_ 1

#include "base.h"

#include <lua/lua.h>
#include <lua/lualib.h>
#include <lua/lauxlib.h>

#define lua_stack_debug(_lua) printf("%s:%d %s(): stack:%d\n"                                   \
                        , __FILE__, __LINE__, __FUNCTION__, lua_gettop(_lua))

#define lua_foreach(_lua, _idx) for(lua_pushnil(_lua); lua_next(_lua, _idx); lua_pop(_lua, 1))

typedef int (*module_callback_t)(module_data_t *);

typedef struct
{
    const char *name;
    module_callback_t method;
} module_method_t;

#define MODULE_OPTIONS_IDX 2

#define MODULE_LUA_METHODS()                                                                    \
    static const module_method_t __module_methods[] =

#define MODULE_LUA_REGISTER(_name)                                                              \
    static const char __module_name[] = #_name;                                                 \
    static int __module_tostring(lua_State *L)                                                  \
    {                                                                                           \
        lua_pushstring(L, __module_name);                                                       \
        return 1;                                                                               \
    }                                                                                           \
    static int __module_thunk(lua_State *L)                                                     \
    {                                                                                           \
        module_data_t *mod = (module_data_t *)lua_touserdata(L, lua_upvalueindex(1));           \
        module_method_t *m = (module_method_t *)lua_touserdata(L, lua_upvalueindex(2));         \
        return m->method(mod);                                                                  \
    }                                                                                           \
    static int __module_delete(lua_State *L)                                                    \
    {                                                                                           \
        module_data_t *mod = (module_data_t *)lua_touserdata(L, lua_upvalueindex(1));           \
        module_destroy(mod);                                                                    \
        free(mod);                                                                              \
        return 0;                                                                               \
    }                                                                                           \
    static int __module_new(lua_State *L)                                                       \
    {                                                                                           \
        size_t i;                                                                               \
        static const luaL_Reg __meta_methods[] =                                                \
        {                                                                                       \
            { "__gc", __module_delete },                                                        \
            { "__tostring", __module_tostring },                                                \
        };                                                                                      \
        module_data_t *mod = (module_data_t *)calloc(1, sizeof(module_data_t));                 \
        lua_newtable(L);                                                                        \
        lua_newtable(L);                                                                        \
        for(i = 0; i < ASC_ARRAY_SIZE(__meta_methods); ++i)                                     \
        {                                                                                       \
            const luaL_Reg *m = &__meta_methods[i];                                             \
            lua_pushlightuserdata(L, (void *)mod);                                              \
            lua_pushcclosure(L, m->func, 1);                                                    \
            lua_setfield(L, -2, m->name);                                                       \
        }                                                                                       \
        lua_setmetatable(L, -2);                                                                \
        for(i = 0; i < ASC_ARRAY_SIZE(__module_methods); ++i)                                   \
        {                                                                                       \
            const module_method_t *m = &__module_methods[i];                                    \
            if(!m->name) break;                                                                 \
            lua_pushlightuserdata(L, (void *)mod);                                              \
            lua_pushlightuserdata(L, (void *)m);                                                \
            lua_pushcclosure(L, __module_thunk, 2);                                             \
            lua_setfield(L, -2, m->name);                                                       \
        }                                                                                       \
        if(lua_gettop(L) == 3)                                                                  \
        {                                                                                       \
            lua_pushvalue(L, MODULE_OPTIONS_IDX);                                               \
            lua_setfield(L, 3, "__options");                                                    \
        }                                                                                       \
        module_init(mod);                                                                       \
        return 1;                                                                               \
    }                                                                                           \
    LUA_API int luaopen_##_name(lua_State *L)                                                   \
    {                                                                                           \
        static const luaL_Reg meta_methods[] =                                                  \
        {                                                                                       \
            { "__tostring", __module_tostring },                                                \
            { "__call", __module_new },                                                         \
            { NULL, NULL }                                                                      \
        };                                                                                      \
        lua_newtable(L);                                                                        \
        lua_newtable(L);                                                                        \
        luaL_setfuncs(L, meta_methods, 0);                                                      \
        lua_setmetatable(L, -2);                                                                \
        lua_setglobal(L, __module_name);                                                        \
        return 1;                                                                               \
    }

bool module_option_number(const char *name, int *number);
bool module_option_string(const char *name, const char **string, size_t *length);
bool module_option_boolean(const char *name, bool *boolean);

#endif /* _MODULE_LUA_H_ */
