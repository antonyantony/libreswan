/* Root certificates, for libreswan
 *
 * Copyright (C) 2015,2018 Matt Rogers <mrogers@libreswan.org>
 * Copyright (C) 2017-2018 Paul Wouters <pwouters@redhat.com>
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

#include <cert.h>

#include "defs.h"
#include "root_certs.h"
#include "server.h"
#include "pluto_timing.h"
#include "lswlog.h"

static CERTCertList *root_certs;

CERTCertList *get_root_certs(void)
{
	passert(in_main_thread());

	/* extend or set cert cache lifetime */
	schedule_oneshot_timer(EVENT_FREE_ROOT_CERTS, FREE_ROOT_CERTS_TIMEOUT);

	if (root_certs != NULL) {
		return root_certs;
	}
	log_to_log("loading root certificate cache");
	/* always set, if things fail then an empty list is returned */
	root_certs = CERT_NewCertList();

	PK11SlotInfo *slot = PK11_GetInternalKeySlot();
	if (slot == NULL) {
		return root_certs;
	}

	if (PK11_NeedLogin(slot)) {
		SECStatus rv = PK11_Authenticate(slot, PR_TRUE,
				lsw_return_nss_password_file_info());
		if (rv != SECSuccess)
			return root_certs;
	}

	/*
	 * This is the killer when it comes to performance.
	 */
	threadtime_t get_time = threadtime_start();
	CERTCertList *allcerts = PK11_ListCertsInSlot(slot);
	threadtime_stop(&get_time, SOS_NOBODY, "%s() calling PK11_ListCertsInSlot()", __func__);
	if (allcerts == NULL)
		return root_certs;

	/*
	 * XXX: would a better call be
	 * CERT_FilterCertListByUsage(allcerts, certUsageAnyCA,
	 * PR_TRUE)?  Timing tests suggest it makes little difference,
	 * and the result is being cached anyway.
	 */
	threadtime_t ca_time = threadtime_start();
	for (CERTCertListNode *node = CERT_LIST_HEAD(allcerts);
	     !CERT_LIST_END(node, allcerts);
	     node = CERT_LIST_NEXT(node)) {
		if (CERT_IsCACert(node->cert, NULL) && node->cert->isRoot) {
			CERTCertificate *dup = CERT_DupCertificate(node->cert);
			CERT_AddCertToListTail(root_certs, dup);
		}
	}
	CERT_DestroyCertList(allcerts);
	threadtime_stop(&ca_time, SOS_NOBODY, "%s() filtering CAs", __func__);

	return root_certs;
}

void init_root_certs(void)
{
	init_oneshot_timer(EVENT_FREE_ROOT_CERTS, free_root_certs);
}

void free_root_certs(void)
{
	passert(in_main_thread());
	if (root_certs != NULL) {
		log_to_log("destroying root certificate cache");
		CERT_DestroyCertList(root_certs);
		root_certs = NULL;
	}
}
