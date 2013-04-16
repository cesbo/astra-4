/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#include "base.h"

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
