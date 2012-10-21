/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#define ASTRA_CORE
#include <astra.h>

#ifndef WITHOUT_MODULES

#define LOG_MSG(_msg) "[core/module] " _msg

struct module_data_s
{
    MODULE_BASE();
};

static int _module_set_option(module_data_t *mod
                              , const module_option_t *opt
                              , const char *val)
{
    char *dst = (char *)mod + opt->offset;
    switch(opt->type)
    {
        case VALUE_NUMBER:
            *(int *)dst = atoi(val);
            break;
        case VALUE_STRING:
            *(char **)dst = (char *)val;
            break;
        case VALUE_NONE:
            break;
        default:
            return 0;
    }
    return (opt->check) ? opt->check(mod) : 1;
}

void module_set_options(module_data_t *mod
                        , const module_option_t *opts, size_t count)
{
    static const char *_set_option_msg = "failed to set option \"%s=%s\" "
                                         "in the module \"%s\"";

    lua_State *L = LUA_STATE(mod);
    for(lua_pushnil(L); lua_next(L, 2); lua_pop(L, 1))
    {
        if(lua_isnumber(L, -2))
            continue;

        const char *var = lua_tostring(L, -2);
        if(var[0] == '_')
            continue;

        const module_option_t *opt = NULL;
        for(size_t i = 0; i < count; ++i)
        {
            opt = &opts[i];
            if(opt->name && !strcmp(opt->name, var))
                break;
            opt = NULL;
        }

        const int type = lua_type(L, -1);
        if(!opt)
        {
#if 0
            if(type != LUA_TNIL)
            {
                log_warning("[core/module] option %s is not found in module %s"
                            , var, mod->__name);
            }
#endif
            continue;
        }

        if(opt->type == VALUE_NONE)
        {
            if(!_module_set_option(mod, opt, NULL))
                luaL_error(L, _set_option_msg, var, "", mod->__name);
        }
        else if(type == LUA_TTABLE)
        {
            for(lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1))
            {
                const char *val = lua_tostring(L, -1);
                if(!_module_set_option(mod, opt, val))
                    luaL_error(L, _set_option_msg, var, val, mod->__name);
            }
        }
        else
        {
            const char *val;
            if(type == LUA_TBOOLEAN)
                val = ( lua_toboolean(L, -1) ) ? "1" : "0";
            else
                val = lua_tostring(L, -1);

            if(!_module_set_option(mod, opt, val))
                luaL_error(L, _set_option_msg, var, val, mod->__name);
        }
    }
}

#endif /* ! WITHOUT_MODULES */
