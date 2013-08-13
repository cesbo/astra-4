/*
 * Astra Module: Lua API
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
