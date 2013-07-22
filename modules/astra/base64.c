
#include "base.h"
#include <stdlib.h>
#include <string.h>

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

static const int mod_table[] = {0, 2, 1};

char * base64_encode(const char *in, size_t size, size_t *out_size)
{
    if(!in)
        return NULL;

    *out_size = ((size + 2) / 3) * 4;

    char *out = malloc(*out_size);

    for(size_t i = 0, j = 0; i < size;)
    {
        uint32_t octet_a = (i < size) ? (uint8_t)in[i++] : 0;
        uint32_t octet_b = (i < size) ? (uint8_t)in[i++] : 0;
        uint32_t octet_c = (i < size) ? (uint8_t)in[i++] : 0;

        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

        out[j++] = base64_list[(triple >> 3 * 6) & 0x3F];
        out[j++] = base64_list[(triple >> 2 * 6) & 0x3F];
        out[j++] = base64_list[(triple >> 1 * 6) & 0x3F];
        out[j++] = base64_list[(triple >> 0 * 6) & 0x3F];
    }

    switch(size % 3)
    {
        case 0:
            break;
        case 1:
            out[*out_size - 2] = '=';
        case 2:
            out[*out_size - 1] = '=';
            break;
    }
    out[*out_size] = '\0';

    return out;
}

char * base64_decode(const char *in, size_t *out_size)
{
    size_t size = 0;
    while(in[size])
        ++size;

    if(size % 4 != 0)
        return NULL;

    *out_size = (size / 4) * 3;

    if(in[size - 1] == '=')
        --(*out_size);
    if(in[size - 2] == '=')
        --(*out_size);

    char *out = malloc(*out_size);

    for(size_t i = 0, j = 0; i < size;)
    {
        uint32_t sextet_a = (in[i] == '=') ? (0 & i++) : base64_index[(uint8_t)in[i++]];
        uint32_t sextet_b = (in[i] == '=') ? (0 & i++) : base64_index[(uint8_t)in[i++]];
        uint32_t sextet_c = (in[i] == '=') ? (0 & i++) : base64_index[(uint8_t)in[i++]];
        uint32_t sextet_d = (in[i] == '=') ? (0 & i++) : base64_index[(uint8_t)in[i++]];

        uint32_t triple = (sextet_a << 3 * 6)
                        + (sextet_b << 2 * 6)
                        + (sextet_c << 1 * 6)
                        + (sextet_d << 0 * 6);

        if (j < *out_size)
            out[j++] = (triple >> 2 * 8) & 0xFF;
        if (j < *out_size)
            out[j++] = (triple >> 1 * 8) & 0xFF;
        if (j < *out_size)
            out[j++] = (triple >> 0 * 8) & 0xFF;
    }
    out[*out_size] = '\0';

    return out;
}

#if 0
#include <stdio.h>
static void TEST(int id, const char *s, const char *r)
{
    size_t s_size = 0 , r_size = 0;
    while(s[s_size])
        ++s_size;
    while(r[r_size])
        ++r_size;

    size_t size = 0;
    char *data = base64_encode(s, s_size, &size);
    if(size != r_size)
    {
        printf("ERROR [%d.1] %ld != %ld\n", id, size, r_size);
        abort();
    }
    if(memcmp(data, r, r_size + 1))
    {
        printf("ERROR [%d.2] %s != %s\n", id, data, r);
        abort();
    }
    free(data);
    data = base64_decode(r, &r_size);
    if(s_size != r_size)
    {
        printf("ERROR [%d.3] %ld != %ld\n", id, s_size, r_size);
        abort();
    }
    if(memcmp(data, s, s_size + 1))
    {
        printf("ERROR [%d.2] %s != %s\n", id, data, s);
        abort();
    }
    free(data);
}

int main(void)
{
    char s1[] = "1234567890"; char r1[] = "MTIzNDU2Nzg5MA==";
    char s2[] = "qwertyuio";  char r2[] = "cXdlcnR5dWlv";
    char s3[] = "ASDFGHJK";   char r3[] = "QVNERkdISks=";

    TEST(1, s1, r1);
    TEST(2, s2, r2);
    TEST(3, s3, r3);

    return 0;
}
#endif
