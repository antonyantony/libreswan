/*
 * @(#) Libreswan tunable paramaters
 *
 * Copyright (C) 2001  Richard Guy Briggs  <rgb@freeswan.org>
 *                 and Michael Richardson  <mcr@freeswan.org>
 * Copyright (C) 2004  Michael Richardson  <mcr@xelerance.com>
 * Copyright (C) 2012  Paul Wouters  <paul@libreswan.org>
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
 *
 */

/*
 * This file provides a set of #defines that may be tuned by various
 * people/configurations. It keeps all compile-time tunables in one place.
 *
 * This file should be included before all other IPsec kernel-only files.
 *
 */

#ifndef _IPSEC_PARAM_H_

/*
 * This is for the SA reference table. This number is related to the
 * maximum number of SAs that KLIPS can concurrently deal with, plus enough
 * space for keeping expired SAs around.
 *
 * TABLE_IDX_WIDTH is the number of bits that we will use.
 * MAIN_TABLE_WIDTH is the number of bits used for the primary index table.
 *
 */
#ifndef IPSEC_SA_REF_MAINTABLE_IDX_WIDTH
# define IPSEC_SA_REF_MAINTABLE_IDX_WIDTH 4
#endif

#ifndef IPSEC_SA_REF_FREELIST_NUM_ENTRIES
# define IPSEC_SA_REF_FREELIST_NUM_ENTRIES 256
#endif

#ifndef IPSEC_SA_REF_CODE
# define IPSEC_SA_REF_CODE 1
#endif

#ifdef NEED_INET_PROTOCOL
#define inet_protocol net_protocol
#endif

#ifndef IPSEC_DEFAULT_TTL
#define IPSEC_DEFAULT_TTL 64
#endif

#define _IPSEC_PARAM_H_
#endif /* _IPSEC_PARAM_H_ */
