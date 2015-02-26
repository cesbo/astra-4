
#include <astra.h>

/*
 *      base64.encode(string)
 *                  - convert data to base64
 *      base64.decode(base64)
 *                  - convert base64 to data
 */

static const char base64_list[] = \
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

#define XX 0

static const uint8_t base64_index[256] =
{
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,62, XX,XX,XX,63,
    52,53,54,55, 56,57,58,59, 60,61,XX,XX, XX,XX,XX,XX,
    XX, 0, 1, 2,  3, 4, 5, 6,  7, 8, 9,10, 11,12,13,14,
    15,16,17,18, 19,20,21,22, 23,24,25,XX, XX,XX,XX,XX,
    XX,26,27,28, 29,30,31,32, 33,34,35,36, 37,38,39,40,
    41,42,43,44, 45,46,47,48, 49,50,51,XX, XX,XX,XX,XX,
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
};

char * base64_encode(const void *in, size_t in_size, size_t *out_size)
{
    if(!in)
        return NULL;

    size_t size = ((in_size + 2) / 3) * 4;

    char *out = (char *)malloc(size + 1);

    for(size_t i = 0, j = 0; i < in_size;)
    {
        uint32_t octet_a = (i < in_size) ? ((uint8_t *)in)[i++] : 0;
        uint32_t octet_b = (i < in_size) ? ((uint8_t *)in)[i++] : 0;
        uint32_t octet_c = (i < in_size) ? ((uint8_t *)in)[i++] : 0;

        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

        out[j++] = base64_list[(triple >> 3 * 6) & 0x3F];
        out[j++] = base64_list[(triple >> 2 * 6) & 0x3F];
        out[j++] = base64_list[(triple >> 1 * 6) & 0x3F];
        out[j++] = base64_list[(triple >> 0 * 6) & 0x3F];
    }

    switch(in_size % 3)
    {
        case 0:
            break;
        case 1:
            out[size - 2] = '=';
        case 2:
            out[size - 1] = '=';
            break;
    }
    out[size] = 0;

    if(out_size)
        *out_size = size;

    return out;
}

void * base64_decode(const char *in, size_t in_size, size_t *out_size)
{
    if(in_size == 0)
    {
        while(in[in_size])
            ++in_size;
    }

    if(in_size % 4 != 0)
        return NULL;

    size_t size = (in_size / 4) * 3;

    if(in[in_size - 2] == '=')
        size -= 2;
    else if(in[in_size - 1] == '=')
        size -= 1;

    uint8_t *out = (uint8_t *)malloc(size);

    for(size_t i = 0, j = 0; i < in_size;)
    {
        uint32_t sextet_a = (in[i] == '=') ? (0 & i++) : base64_index[(uint8_t)in[i++]];
        uint32_t sextet_b = (in[i] == '=') ? (0 & i++) : base64_index[(uint8_t)in[i++]];
        uint32_t sextet_c = (in[i] == '=') ? (0 & i++) : base64_index[(uint8_t)in[i++]];
        uint32_t sextet_d = (in[i] == '=') ? (0 & i++) : base64_index[(uint8_t)in[i++]];

        uint32_t triple = (sextet_a << 3 * 6)
                        + (sextet_b << 2 * 6)
                        + (sextet_c << 1 * 6)
                        + (sextet_d << 0 * 6);

        if (j < size)
            out[j++] = (triple >> 2 * 8) & 0xFF;
        if (j < size)
            out[j++] = (triple >> 1 * 8) & 0xFF;
        if (j < size)
            out[j++] = (triple >> 0 * 8) & 0xFF;
    }

    if(out_size)
        *out_size = size;

    return out;
}


static int lua_base64_encode(lua_State *L)
{
    const char *data = luaL_checkstring(L, 1);
    const int data_size = luaL_len(L, 1);

    size_t data_enc_size = 0;
    const char *data_enc = base64_encode(data, data_size, &data_enc_size);
    lua_pushlstring(lua, data_enc, data_enc_size);

    free((void *)data_enc);
    return 1;
}

static int lua_base64_decode(lua_State *L)
{
    const char *data = luaL_checkstring(L, 1);
    int data_size = luaL_len(lua, 1);

    size_t data_dec_size = 0;
    const char *data_dec = base64_decode(data, data_size, &data_dec_size);
    lua_pushlstring(lua, data_dec, data_dec_size);

    free((void *)data_dec);
    return 1;
}

LUA_API int luaopen_base64(lua_State *L)
{
    lua_getglobal(L, "string");

    lua_pushcfunction(L, lua_base64_encode);
    lua_setfield(L, -2, "b64e");
    lua_pushcfunction(L, lua_base64_decode);
    lua_setfield(L, -2, "b64d");

    lua_pop(L, 1); // string

    static const luaL_Reg api[] =
    {
        { "encode", lua_base64_encode },
        { "decode", lua_base64_decode },
        { NULL, NULL }
    };

    luaL_newlib(L, api);
    lua_setglobal(L, "base64");

    return 0;
}
