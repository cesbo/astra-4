/*
 * Astra Module: PostgreSQL
 * http://cesbo.com/astra
 *
 * Copyright (C) 2015, NoSFeRaTU <master.nosferatu@gmail.com>
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
#include <postgresql/libpq-events.h>

#define MSG(_msg) "[postgres] " _msg

typedef struct
{
    int             callback;
    char           *query;
    bool            queued;
} query_item_t;

struct module_data_t
{
    const char *connect_string;

    PGconn *conn;
    asc_list_t *query;
    asc_timer_t *query_timer;
    query_item_t *current_task;
    int is_connected;
    int conn_last_state;
};

static void pg_connection_poll(module_data_t *mod)
{
    int state = PQconnectPoll(mod->conn);

    if(state == mod->conn_last_state)
        return;

    mod->conn_last_state = state;

    switch(state)
    {
        case PGRES_POLLING_FAILED:
            asc_log_info(MSG("Connection to database has been failed"));
            mod->is_connected = -1;
            if(mod->current_task)
                mod->current_task->queued = 0;
            break;
        case PGRES_POLLING_OK:
            asc_log_info(MSG("Successfully connected to database"));
            mod->is_connected = 1;
            break;
        case PGRES_POLLING_ACTIVE:
        case PGRES_POLLING_READING:
        case PGRES_POLLING_WRITING:
            break;
    }
}

static char * pg_error(module_data_t *mod, bool show_msg)
{
    char *msg = PQerrorMessage(mod->conn);
    if(msg[0])
    {
        if(show_msg)
            asc_log_error(MSG("%s"), msg);

        return msg;
    }

    return NULL;
}

static void pg_connect(module_data_t *mod)
{
    mod->is_connected = 0;
    mod->conn_last_state = -1;
    mod->conn = PQconnectStart(mod->connect_string);
    PQsetnonblocking(mod->conn, 1);
    pg_connection_poll(mod);
}

static void process_query(module_data_t *mod)
{
    if(!mod->current_task)
    {
        asc_list_for(mod->query)
        {
            mod->current_task = asc_list_data(mod->query);
            asc_list_remove_current(mod->query);
            break;
        }
    }

    if(mod->current_task)
    {
        if(!mod->current_task->queued)
        {
            if(PQsendQuery(mod->conn, mod->current_task->query))
                mod->current_task->queued = 1;
        }

        if(PQconsumeInput(mod->conn))
        {
            // When return code of PQisBusy is zero then PQgetResult is nonblocking
            if(!PQisBusy(mod->conn))
            {
                PGresult *res = PQgetResult(mod->conn);
                if(res)
                {
                    char *msg = pg_error(mod, false);

                    lua_newtable(lua);

                    if(msg)
                    {
                        lua_pushstring(lua, msg);
                        lua_setfield(lua, -2, "error");
                    }
                    else
                    {
                        lua_newtable(lua);
                        for(int i = 0; i < PQnfields(res); i++)
                        {
                            lua_pushstring(lua, PQfname(res, i));
                            lua_rawseti(lua, -2, i+1);
                        }
                        lua_setfield(lua, -2, "columns");

                        lua_newtable(lua);
                        for(int i = 0; i < PQntuples(res); i++)
                        {
                            lua_newtable(lua);
                            for(int f = 0; f < PQnfields(res); f++)
                            {
                                lua_pushstring(lua, PQgetvalue(res, i, f));
                                lua_rawseti(lua, -2, f+1);
                            }
                            lua_rawseti(lua, -2, i+1);
                        }

                        lua_setfield(lua, -2, "data");
                    }

                    lua_rawgeti(lua, LUA_REGISTRYINDEX, mod->current_task->callback);
                    lua_pushvalue(lua, -2);

                    lua_call(lua, 1, 0);

                    lua_pop(lua, 1);

                    luaL_unref(lua, LUA_REGISTRYINDEX, mod->current_task->callback);
                    free(mod->current_task);
                    mod->current_task = NULL;

                    PQclear(res);
                }
            }
        }
        else
            pg_error(mod, true);

    }
}

static void on_query_timer(void *arg)
{
    module_data_t *mod = arg;

    pg_connection_poll(mod);
    if(mod->is_connected == -1)
        pg_connect(mod);
    else if(mod->is_connected == 1)
        process_query(mod);
}

static int method_query(module_data_t *mod)
{
    query_item_t *query_item = calloc(1, sizeof(query_item_t));

    if(lua_istable(lua, -1))
    {
        lua_getfield(lua, -1, "query");
        if(lua_isstring(lua, -1))
            query_item->query = (char*)lua_tostring(lua, -1);
        lua_pop(lua, 1);

        lua_getfield(lua, -1, "callback");
        if(lua_isfunction(lua, -1))
            query_item->callback = luaL_ref(lua, LUA_REGISTRYINDEX);
        else
            asc_log_error("option 'callback' is required");
        lua_pop(lua, 1);
    }

    if(query_item->query && query_item->callback)
    {
        asc_list_insert_tail(mod->query, query_item);
        if(mod->is_connected == 1)
            process_query(mod);
    }
    else
        free(query_item);

    return 0;
}

static void module_init(module_data_t *mod)
{
    module_option_string("connect_string", &mod->connect_string, NULL);
    asc_assert(mod->connect_string, MSG("option 'connect_string' is required"));

    pg_connect(mod);

    mod->query = asc_list_init();
    mod->query_timer = asc_timer_init(50, on_query_timer, mod);
}

static void module_destroy(module_data_t *mod)
{
    if(mod->conn)
        PQfinish(mod->conn);

    if(mod->query_timer)
    {
        asc_timer_destroy(mod->query_timer);
        mod->query_timer = NULL;
    }

    if(mod->query)
    {
        asc_list_for(mod->query)
        {
            query_item_t *query_item = asc_list_data(mod->query);
            luaL_unref(lua, LUA_REGISTRYINDEX, query_item->callback);
            free(query_item);
            asc_list_remove_current(mod->query);
        }

        asc_list_destroy(mod->query);
        mod->query = NULL;
    }

    if(mod->current_task)
    {
        luaL_unref(lua, LUA_REGISTRYINDEX, mod->current_task->callback);
        free(mod->current_task);
        mod->current_task = NULL;
    }
}

MODULE_LUA_METHODS()
{
    { "query", method_query },
};
MODULE_LUA_REGISTER(postgres)
