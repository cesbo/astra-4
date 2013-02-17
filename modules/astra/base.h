/*
 * Astra Base Modules
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#ifndef _ASTRA_BASE_H_
#define _ASTRA_BASE_H_ 1

#include <astra.h>

#include "module_event.h"

char * hex_to_str(char *, const uint8_t *, size_t);
uint8_t * str_to_hex(const char *, uint8_t *, size_t);

#endif /* _ASTRA_BASE_H_ */
