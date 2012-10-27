/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#ifndef _ASTRA_H_
#define _ASTRA_H_ 1

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "../version.h"
#define __VSTR(_x) #_x
#define _VSTR(_x) __VSTR(_x)
#define _VERSION "v." _VSTR(ASTRA_VERSION) "." _VSTR(ASTRA_VERSION_DEV)
#ifdef DEBUG
#   define _VDEBUG " debug"
#else
#   define _VDEBUG
#endif
#define ASTRA_VERSION_STR _VERSION _VDEBUG

#ifdef _WIN32
#   if defined(ASTRA_CORE)
#       define ASTRA_API __declspec(dllexport)
#   elif defined(ASTRA_PLUGIN)
#       define ASTRA_API __declspec(dllimport)
#   else
#       define ASTRA_API
#   endif
#else
#   define ASTRA_API extern
#endif

typedef struct list_s list_t;

#define ARRAY_SIZE(_a) (sizeof(_a)/sizeof(_a[0]))
#define CHECK_RET(_cond, _ret)                                              \
    if(_cond)                                                               \
    {                                                                       \
        log_error("%s:%d %s(): " #_cond, __FILE__, __LINE__, __FUNCTION__); \
        _ret;                                                               \
    }

#ifndef WITHOUT_LUA
#   define WITH_LUA
#endif

#ifdef WITH_LUA
#   include <lua.h>
#   include <lualib.h>
#   include <lauxlib.h>

#   define STACK_DEBUG(_L, _pos)                                            \
       printf("%s(): stack %d: %d\n", __FUNCTION__, _pos, lua_gettop(_L))

#   define __LUA_INIT()                                                     \
        lua_State *L = luaL_newstate();                                     \
        luaL_openlibs(L)

#   define __LUA_DESTROY()                                                  \
        lua_close(L)

#else
#   define __LUA_INIT() (void)
#   define __LUA_DESTROY() (void)
#endif

#ifndef WITHOUT_MODULES

typedef struct module_data_s module_data_t;
typedef struct module_option_s module_option_t;

// TODO: move to config.h
#include <modules/protocols.h>

/* ------- */

/* modules */

typedef void (*interface_t)(module_data_t *, ...);

#define MODULE_INTERFACE(_id, _callback)                                    \
    mod->__interface[_id] = (interface_t)_callback

#define MODULE_BASE()                                                       \
    lua_State *__L;                                                         \
    const char *__name;                                                     \
    int __idx_self;                                                         \
    int __idx_options;                                                      \
    interface_t __interface[16];                                            \
    struct { ASTRA_PROTOCOLS } __protocols

#define LUA_STATE(_mod) _mod->__L

typedef enum
{
    VALUE_NONE        = 0,
    VALUE_NUMBER      = 1,
    VALUE_STRING      = 2,
} value_type_t;

struct module_option_s
{
    value_type_t type;
    const char *name;
    size_t offset;
    int is_required;
    int default_number;
    const char *default_string;
    int (*check)(module_data_t *); // return 0 on error, otherwise return 1
};

#define MODULE_OPTIONS()                                                    \
    static const module_option_t __module_options[] =

#define _OPTION(_type, _name, _offset, _is_req, _def_n, _def_s)             \
    {                                                                       \
        .type = _type,                                                      \
        .name = _name,                                                      \
        .offset = offsetof(module_data_t, _offset),                         \
        .is_required = _is_req,                                             \
        .default_number = _def_n,                                           \
        .default_string = _def_s,                                           \
        .check = NULL                                                       \
    },


#define OPTION_NUMBER(_name, _offset, _is_req, _def_n)                      \
    _OPTION(VALUE_NUMBER, _name, _offset, _is_req, _def_n, NULL)
#define OPTION_STRING(_name, _offset, _is_req, _def_s)                      \
    _OPTION(VALUE_STRING, _name, _offset, _is_req, 0, _def_s)
#define OPTION_CUSTOM(_name, _check, _is_req)                               \
    { VALUE_NONE, _name, 0, _is_req, 0, NULL, _check },
#define MODULE_OPTIONS_EMPTY()                                              \
    MODULE_OPTIONS() {{ VALUE_NONE, NULL, 0, 0, 0, NULL, NULL }}

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
        if(__module_options[0].name)                                        \
        {                                                                   \
            if(lua_type(L, 2) == LUA_TTABLE                                 \
               && module_set_options(mod, __module_options                  \
                                     , ARRAY_SIZE(__module_options)))       \
            {                                                               \
                lua_pushvalue(L, 2);                                        \
                mod->__idx_options = luaL_ref(L, LUA_REGISTRYINDEX);        \
            }                                                               \
            else                                                            \
                luaL_error(L, "[core] failed to set options");              \
        }                                                                   \
        module_init(mod);                                                   \
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

ASTRA_API int module_set_options(module_data_t *
                                 , const module_option_t *, int);

#endif /* ! WITHOUT_MODULES */

/* event.c */

enum
{
    EVENT_NONE      = 0x00
    , EVENT_READ    = 0x01
    , EVENT_WRITE   = 0x02
    , EVENT_ERROR   = 0xF0
};

ASTRA_API int event_init(void);
ASTRA_API void event_action(void);
ASTRA_API void event_destroy(void);

ASTRA_API int event_attach(int, void (*)(void *, int), void *, int);
ASTRA_API void event_detach(int);

/* timer.c */

ASTRA_API void timer_action(void);
ASTRA_API void timer_destroy(void);

ASTRA_API void timer_one_shot(unsigned int, void (*)(void *), void *);

ASTRA_API void * timer_attach(unsigned int, void (*)(void *), void *);
ASTRA_API void timer_detach(void *);

/* list.c */

ASTRA_API list_t * list_append(list_t *, void *);
ASTRA_API list_t * list_insert(list_t *, void *);
ASTRA_API list_t * list_delete(list_t *, void *);
ASTRA_API list_t * list_get_first(list_t *);
ASTRA_API list_t * list_get_next(list_t *);
ASTRA_API void * list_get_data(list_t *);

/* log.c */

#ifdef WITH_LUA
ASTRA_API void log_init(lua_State *);
#   define __LOG_INIT() log_init(L)
#else
#   define __LOG_INIT() (void)
#endif

ASTRA_API void log_hup(void);
ASTRA_API void log_destroy(void);

ASTRA_API void log_info(const char *, ...);
ASTRA_API void log_error(const char *, ...);
ASTRA_API void log_warning(const char *, ...);
ASTRA_API void log_debug(const char *, ...);

/* socket.c */

enum
{
    SOCKET_FAMILY_IPv4      = 0x00000001,
    SOCKET_FAMILY_IPv6      = 0x00000002,
    SOCKET_PROTO_TCP        = 0x00000004,
    SOCKET_PROTO_UDP        = 0x00000008,
    SOCKET_REUSEADDR        = 0x00000010,
    SOCKET_BLOCK            = 0x00000020,
    SOCKET_BROADCAST        = 0x00000040,
    SOCKET_LOOP_DISABLE     = 0x00000080,
    SOCKET_BIND             = 0x00000100,
    SOCKET_CONNECT          = 0x00000200,
    SOCKET_NO_DELAY         = 0x00000400,
    SOCKET_KEEP_ALIVE       = 0x00000800,
};

enum
{
    SOCKET_SHUTDOWN_RECV    = 1,
    SOCKET_SHUTDOWN_SEND    = 2,
    SOCKET_SHUTDOWN_BOTH    = 3,
};

ASTRA_API void socket_init(void);
ASTRA_API void socket_destroy(void);

ASTRA_API int socket_open(int, const char *, int);
ASTRA_API int socket_shutdown(int, int);
ASTRA_API void socket_close(int);
ASTRA_API char * socket_error(void);

ASTRA_API int socket_options_set(int, int);
ASTRA_API int socket_port(int);

ASTRA_API int socket_accept(int, char *, int *);

ASTRA_API int socket_set_buffer(int, int, int);
ASTRA_API int socket_set_timeout(int, int, int);

ASTRA_API int socket_multicast_join(int, const char *, const char *);
ASTRA_API int socket_multicast_leave(int, const char *);
ASTRA_API int socket_multicast_renew(int, const char *, const char *);
ASTRA_API int socket_multicast_set_ttl(int, int);
ASTRA_API int socket_multicast_set_if(int, const char *);

ASTRA_API ssize_t socket_recv(int, void *, size_t);
ASTRA_API ssize_t socket_send(int, void *, size_t);

ASTRA_API void * socket_sockaddr_init(const char *, int);
ASTRA_API void socket_sockaddr_destroy(void *sockaddr);

ASTRA_API ssize_t socket_recvfrom(int, void *, size_t, void *);
ASTRA_API ssize_t socket_sendto(int, void *, size_t, void *);

/* stream.c */

typedef struct stream_s stream_t;

ASTRA_API stream_t * stream_init(void (*)(void *), void *);
ASTRA_API void stream_destroy(stream_t *);

ASTRA_API ssize_t stream_send(stream_t *, void *, size_t);
ASTRA_API ssize_t stream_recv(stream_t *, void *, size_t);

/* thread.c */

typedef struct thread_s thread_t;

ASTRA_API int thread_init(thread_t **, void (*)(void *), void *);
ASTRA_API void thread_destroy(thread_t **);

int thread_is_started(thread_t *);

/* */

#define ASTRA_CORE_INIT()                                                   \
    __LUA_INIT();                                                           \
    __LOG_INIT();                                                           \
    socket_init();                                                          \
    event_init()

#define ASTRA_CORE_LOOP(_cond)                                              \
    while(_cond)                                                            \
    {                                                                       \
        event_action();                                                     \
        timer_action();                                                     \
    }

#define ASTRA_CORE_DESTROY()                                                \
    __LUA_DESTROY();                                                        \
    timer_destroy();                                                        \
    event_destroy();                                                        \
    socket_destroy();                                                       \
    log_info("[main] exit");                                                \
    log_destroy()

#endif /* _ASTRA_H_ */
