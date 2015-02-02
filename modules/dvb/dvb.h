/*
 * Astra Module: DVB
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

#ifndef DTV_STREAM_ID
	#define DTV_STREAM_ID DTV_ISDBS_TS_ID
#endif
#ifndef NO_STREAM_ID_FILTER
	#define NO_STREAM_ID_FILTER	(~0U)
#endif

#define DVB_API ((DVB_API_VERSION * 100) + DVB_API_VERSION_MINOR)

#endif /* _DVB_H_ */
