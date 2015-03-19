/*
 * RC4
 * Copyright (c) 1996-2000 Whistle Communications, Inc.
 */

/*
 *      (string):rc4(key)
 *                  - encrypt/decrypt
 */

#include <astra.h>

typedef struct
{
    uint8_t perm[256];
    uint8_t index1;
    uint8_t index2;
} rc4_ctx_t;

static __inline void rc4_swap_bytes(uint8_t *a, uint8_t *b)
{
    uint8_t temp;
    temp = *a;
    *a = *b;
    *b = temp;
}

static void rc4_init(rc4_ctx_t *state, const uint8_t *key, int keylen)
{
    uint8_t j;
    int i;

    for(i = 0; i < 256; i++)
        state->perm[i] = (uint8_t)i;

    state->index1 = 0;
    state->index2 = 0;

    for(j = i = 0; i < 256; i++)
    {
        j += state->perm[i] + key[i % keylen];
        rc4_swap_bytes(&state->perm[i], &state->perm[j]);
    }
}

static void rc4_crypt(rc4_ctx_t *state, uint8_t *dst, const uint8_t *buf, int buflen)
{
    int i;
    uint8_t j;

    for (i = 0; i < buflen; i++)
    {
        state->index1++;
        state->index2 += state->perm[state->index1];

        rc4_swap_bytes(&state->perm[state->index1], &state->perm[state->index2]);

        j = state->perm[state->index1] + state->perm[state->index2];
        dst[i] = buf[i] ^ state->perm[j];
    }
}

static int lua_rc4(lua_State *L)
{
    const uint8_t *data = (const uint8_t *)luaL_checkstring(L, 1);
    const int data_size = luaL_len(L, 1);

    const uint8_t *key = (const uint8_t *)luaL_checkstring(L, 2);
    const int key_size = luaL_len(L, 2);

    rc4_ctx_t ctx;
    rc4_init(&ctx, key, key_size);

    luaL_Buffer b;
    char *p = luaL_buffinitsize(L, &b, data_size);
    rc4_crypt(&ctx, (uint8_t *)p, data, data_size);
    luaL_addsize(&b, data_size);
    luaL_pushresult(&b);

    memset(&ctx, 0, sizeof(ctx));

    return 1;
}

LUA_API int luaopen_rc4(lua_State *L)
{
    lua_getglobal(L, "string");

    lua_pushcfunction(L, lua_rc4);
    lua_setfield(L, -2, "rc4");

    lua_pop(L, 1); // string

    return 0;
}
