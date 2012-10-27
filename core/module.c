/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#define ASTRA_CORE
#include <astra.h>

#ifndef WITHOUT_MODULES

struct module_data_s
{
    MODULE_BASE();
};

int module_set_options(module_data_t *mod
                       , const module_option_t *opts, int count)
{
    lua_State *L = LUA_STATE(mod);
    for(int i = 0; i < count; ++i)
    {
        const module_option_t *opt = &opts[i];

        lua_getfield(L, 2, opt->name);
        int type = lua_type(L, -1);
        if(type == LUA_TNIL)
        {
            if(opt->is_required)
            {
                log_error("[%s] option \"%s\" is required"
                          , mod->__name, opt->name);
                lua_pop(L, 1);
                return 0;
            }

            char *dst = (char *)mod + opt->offset;
            switch(opt->type)
            {
                case VALUE_NUMBER:
                    *(int *)dst = opt->default_number;
                    break;
                case VALUE_STRING:
                    *(const char **)dst = opt->default_string;
                    break;
                case VALUE_NONE:
                    break;
            }
        }
        else
        {
            char *dst = (char *)mod + opt->offset;
            switch(opt->type)
            {
                case VALUE_NUMBER:
                {
                    if(type == LUA_TNUMBER)
                        *(int *)dst = lua_tonumber(L, -1);
                    else if(type == LUA_TBOOLEAN)
                        *(int *)dst = lua_toboolean(L, -1);
                    else
                    {
                        log_error("[%s] for option \"%s\" "
                                  "number or boolean is required"
                                  , mod->__name, opt->name);
                        lua_pop(L, 1);
                        return 0;
                    }
                    break;
                }
                case VALUE_STRING:
                {
                    if(type != LUA_TSTRING)
                    {
                        log_error("[%s] for option \"%s\" string is required"
                                  , mod->__name, opt->name);
                        lua_pop(L, 1);
                        return 0;
                    }
                    *(const char **)dst = lua_tostring(L, -1);
                    break;
                }
                case VALUE_NONE:
                {
                    if(opt->check && !opt->check(mod))
                    {
                        lua_pop(L, 1);
                        return 0;
                    }
                    break;
                }
            }
        }
        lua_pop(L, 1); // field
    }

    return 1;
}

#endif /* ! WITHOUT_MODULES */
