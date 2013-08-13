/*
 * Astra Module: Str2Hex
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

/*
 *      (string):hex()
 *                  - dump data in hex
 */

#include <astra.h>

char * hex_to_str(char *str, const uint8_t *data, int size)
{
    static const char char_str[] = "0123456789ABCDEF";
    for(int i = 0; i < size; i++)
    {
        const int j = i * 2;
        str[j + 0] = char_str[data[i] >> 4];
        str[j + 1] = char_str[data[i] & 0x0F];
    }
    str[size * 2] = '\0';
    return str;
}

inline static uint8_t single_char_to_hex(char c)
{
    if(c >= '0' && c <= '9')
        return c - '0';
    else if(c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    else if(c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return 0;
}

inline static uint8_t char_to_hex(const char *c)
{
    return (single_char_to_hex(c[0]) << 4) | single_char_to_hex(c[1]);
}

uint8_t * str_to_hex(const char *str, uint8_t *data, int size)
{
    if(!size)
        size = ~0;

    for(int i = 0; str[0] && str[1] && i < size; str += 2, ++i)
        data[i] = char_to_hex(str);

    return data;
}

static int lua_hex(lua_State *L)
{
    const uint8_t *data = (const uint8_t *)luaL_checkstring(L, 1);
    const int data_size = luaL_len(L, 1);

    luaL_Buffer b;
    char *p = luaL_buffinitsize(L, &b, data_size * 2 + 1);
    hex_to_str(p, data, data_size);
    luaL_addsize(&b, data_size * 2);
    luaL_pushresult(&b);

    return 1;
}

LUA_API int luaopen_str2hex(lua_State *L)
{
    lua_getglobal(L, "string");

    lua_pushvalue(L, -1);
    lua_pushcfunction(L, lua_hex);
    lua_setfield(L, -2, "hex");

    lua_pop(L, 1); // string

    return 1;
}
