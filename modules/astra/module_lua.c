/*
 * Astra Module Lua API
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#include <astra.h>

lua_State *lua = NULL;

int module_option_number(const char *name, int *number)
{
    do
    {
        if(lua_type(lua, MODULE_OPTIONS_IDX) != LUA_TTABLE)
            break;
        lua_getfield(lua, MODULE_OPTIONS_IDX, name);
        const int type = lua_type(lua, -1);
        if(type == LUA_TNUMBER)
        {
            *number = lua_tonumber(lua, -1);
            lua_pop(lua, 1);
            return true;
        }
        else if(type == LUA_TSTRING)
        {
            const char *str = lua_tostring(lua, -1);
            *number = atoi(str);
            lua_pop(lua, 1);
            return true;
        }
        else if(type == LUA_TBOOLEAN)
        {
            *number = lua_toboolean(lua, -1);
            lua_pop(lua, 1);
            return true;
        }
        else
            lua_pop(lua, 1);
    } while(0);

    return false;
}

int module_option_string(const char *name, const char **string)
{
    do
    {
        if(lua_type(lua, MODULE_OPTIONS_IDX) != LUA_TTABLE)
            break;
        lua_getfield(lua, MODULE_OPTIONS_IDX, name);
        if(lua_type(lua, -1) == LUA_TSTRING)
        {
            const int length = luaL_len(lua, -1);
            *string = lua_tostring(lua, -1);
            lua_pop(lua, 1);
            return length;
        }
        else
            lua_pop(lua, 1);
    } while(0);

    return 0;
}
