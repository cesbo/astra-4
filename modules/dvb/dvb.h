/*
 * Astra DVB Module
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#ifndef _DVB_H_
#define _DVB_H_ 1

#include <astra.h>

#include <sys/ioctl.h>
#include <linux/dvb/version.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/ca.h>

#if DVB_API_VERSION < 5
#   error "DVB_API_VERSION < 5"
#endif

#endif /* _DVB_H_ */
