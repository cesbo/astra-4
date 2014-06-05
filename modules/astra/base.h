/*
 * Astra Module: Base
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
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

#ifndef _ASTRA_BASE_H_
#define _ASTRA_BASE_H_ 1

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct module_data_t module_data_t;

/* utils */

char * hex_to_str(char *str, const uint8_t *hex, int len);
uint8_t * str_to_hex(const char *str, uint8_t *hex, int len);

/* crc32b.c */

#define CRC32_SIZE 4
uint32_t crc32b(const uint8_t *buffer, int size);

/* sha1.c */

typedef struct
{
    uint32_t state[5];
    uint32_t count[2];
    uint8_t  buffer[64];
} sha1_ctx_t;

#define SHA1_DIGEST_SIZE 20

void sha1_init(sha1_ctx_t *context);
void sha1_update(sha1_ctx_t *context, const uint8_t* data, size_t len);
void sha1_final(sha1_ctx_t *context, uint8_t digest[SHA1_DIGEST_SIZE]);

/* base64.c */

char * base64_encode(const void *in, size_t in_size, size_t *out_size);
void * base64_decode(const char *in, size_t in_size, size_t *out_size);

/* md5.c */

typedef struct
{
    uint32_t state[4];  /* state (ABCD) */
    uint32_t count[2];  /* number of bits, modulo 2^64 (lsb first) */
    uint8_t buffer[64]; /* input buffer */
} md5_ctx_t;

#define MD5_DIGEST_SIZE 16

void md5_init(md5_ctx_t *context);
void md5_update(md5_ctx_t *context, const uint8_t *data, size_t len);
void md5_final(md5_ctx_t *context, uint8_t digest[MD5_DIGEST_SIZE]);

void md5_crypt(const char *pw, const char *salt, char passwd[36]);

/* iso8859.c */

char * iso8859_decode(const uint8_t *data, size_t size);

#endif /* _ASTRA_BASE_H_ */
