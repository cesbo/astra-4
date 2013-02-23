/*
 * Astra Base Modules
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#ifndef _ASTRA_BASE_H_
#define _ASTRA_BASE_H_ 1

#include <stddef.h>
#include <stdint.h>

typedef struct module_data_s module_data_t;

/* utils */

char * hex_to_str(char *str, const uint8_t *hex, size_t len);
uint8_t * str_to_hex(const char *str, uint8_t *hex, size_t len);

/* crc32b.c */

#define CRC32_SIZE 4
uint32_t crc32b(const uint8_t *, size_t);

#endif /* _ASTRA_BASE_H_ */
