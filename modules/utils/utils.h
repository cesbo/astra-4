/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#ifndef _UTILS_H_
#define _UTILS_H_ 1

#include <stddef.h>
#include <stdint.h>

/* strhex.c */
char * hex_to_str(char *, const uint8_t *, size_t);
uint8_t * str_to_hex(const char *, uint8_t *, size_t);

#endif
