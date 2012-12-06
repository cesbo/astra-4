/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#include <astra.h>

struct module_data_s
{
    MODULE_BASE();
};

static void _option_required(module_data_t *mod, const char *name)
{
    log_error("[%s] option \"%s\" is required", mod->__name, name);
    abort();
}

int module_set_number(module_data_t *mod, const char *name, int is_required
                      , int def, int *dst)
{
    lua_State *L = LUA_STATE(mod);
    do
    {
        if(lua_type(L, 2) != LUA_TTABLE)
            break;
        lua_getfield(L, 2, name);
        const int type = lua_type(L, -1);
        if(type == LUA_TNUMBER)
        {
            *dst = lua_tonumber(L, -1);
            lua_pop(L, 1);
            return 1;
        }
        else if(type == LUA_TSTRING)
        {
            const char *str = lua_tostring(L, -1);
            *dst = atoi(str);
            lua_pop(L, 1);
            return 1;
        }
        else if(type == LUA_TBOOLEAN)
        {
            *dst = lua_toboolean(L, -1);
            lua_pop(L, 1);
            return 1;
        }
        else
            lua_pop(L, 1);
    } while(0);

    if(is_required)
        _option_required(mod, name);
    *dst = def;
    return 0;
}

int module_set_string(module_data_t *mod, const char *name, int is_required
                      , const char *def, const char **dst)
{
    lua_State *L = LUA_STATE(mod);
    do
    {
        if(lua_type(L, 2) != LUA_TTABLE)
            break;
        lua_getfield(L, 2, name);
        if(lua_type(L, -1) == LUA_TSTRING)
        {
            *dst = lua_tostring(L, -1);
            lua_pop(L, 1);
            return 1;
        }
        else
            lua_pop(L, 1);
    } while(0);

    if(is_required)
        _option_required(mod, name);
    *dst = def;
    return 0;
}
