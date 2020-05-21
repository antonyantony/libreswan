/* misc. universal things, for libreswan
 *
 * Copyright (C) 1997 Angelos D. Keromytis.
 * Copyright (C) 1998-2001  D. Hugh Redelmeier.
 * Copyright (C) 2018-2019 Andrew Cagney <cagney@gnu.org>
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
 *
 */

#ifndef _DEFS_H
#define _DEFS_H

#include "lswcdefs.h"
#include "lswalloc.h"
#include "realtime.h"

/*
 * Type of serial number of a state object.
 *
 * Used everywhere as a safe proxy for a state object.  Needed in
 * connections.h and state.h; here to simplify dependencies.
 */
typedef unsigned long so_serial_t;
#define SOS_NOBODY      0       /* null serial number */
#define SOS_FIRST       1       /* first normal serial number */

enum sa_type {
#define SA_TYPE_FLOOR 0
	IKE_SA = SA_TYPE_FLOOR,
	IPSEC_SA,
#define SA_TYPE_ROOF (IPSEC_SA+1)
};

extern enum_names v1_sa_type_names;
extern enum_names v2_sa_type_names;
extern enum_enum_names sa_type_names;


/* warns a predefined interval before expiry */
extern const char *check_expiry(realtime_t expiration_date,
				time_t warning_interval, bool strict);

/*
 * Messily exit Pluto
 *
 * This code tries to shutdown pluto while the event loop is still
 * active and while worker threads are still running.  Funny enough,
 * it occasionally crashes or spews garbage, for instance:
 *
 * - a worker thread trying to access NSS after NSS has been shutdown
 *
 * - scary leak-detective errors because there are events sitting in
 *   the event queue
 *
 * The global EXITING_PLUTO is there as a hint to long running threads
 * that they should also shutdown (it should be tested in the thread's
 * main and some inner loops).  Just note that, on its own, it isn't
 * sufficient.  Any long running threads will also need a gentle nudge
 * (so that they loop around and detect the need to quit) and then a
 * join to confirm that they have exited.
 *
 * Also avoid pthread_cancel() which can crash.
 */

extern volatile bool exiting_pluto;
extern void exit_pluto(enum pluto_exit_code status) NEVER_RETURNS;

typedef uint32_t msgid_t;      /* Host byte ordered */
#define PRI_MSGID "%"PRIu32
#define v1_MAINMODE_MSGID  ((msgid_t) 0)		/* network and host order */
#define v2_FIRST_MSGID  ((msgid_t) 0)			/* network and host order */
#define v2_INVALID_MSGID  ((msgid_t) 0xffffffff)	/* network and host order */


/* are all bytes 0? */
extern bool all_zero(const unsigned char *m, size_t len);

/* pad_up(n, m) is the amount to add to n to make it a multiple of m */
#define pad_up(n, m) (((m) - 1) - (((n) + (m) - 1) % (m)))

/* a macro to discard the const portion of a variable to avoid
 * otherwise unavoidable -Wcast-qual warnings.
 * USE WITH CAUTION and only when you know it's safe to discard the const
 */
#ifdef __GNUC__
#define DISCARD_CONST(vartype, \
		      varname) (__extension__({ const vartype tmp = (varname); \
						(vartype)(uintptr_t)tmp; }))
#else
#define DISCARD_CONST(vartype, varname) ((vartype)(uintptr_t)(varname))
#endif

extern bool in_main_thread(void);	/* in plutomain.c */

#endif /* _DEFS_H */
