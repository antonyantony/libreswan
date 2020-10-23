/* logging, for libreswan
 *
 * Copyright (C) 2020 Andrew Cagney
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <https://www.gnu.org/licenses/gpl2.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include <stdarg.h>
#include <stdarg.h>

#include "lswlog.h"

void log_va_list(lset_t rc_flags, const struct logger *logger,
		 const char *message, va_list ap)
{
	if (logger != NULL) {
		LOG_JAMBUF(rc_flags, logger, buf) {
			jam_va_list(buf, message, ap);
		}
	} else {
		/* should not happen; but get the word out */
		JAMBUF(buf) {
			jam_cur_prefix(buf);
			if (DBGP(DBG_BASE)) {
				/* debugging so complain */
				jam(buf, "[EXPECTATION FAILED: logger != NULL] ");
			}
			jam_va_list(buf, message, ap);
			jambuf_to_error_stream(buf);
		}
	}
}
