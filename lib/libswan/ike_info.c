/*
 * Algorithm info parsing and creation functions
 * Author: JuanJo Ciarlante <jjo-ipsec@mendoza.gov.ar>
 *
 * Copyright (C) 2012 Paul Wouters <paul@libreswan.org>
 * Copyright (C) 2015-2019 Andrew Cagney <cagney@gnu.org>
 * Copyright (C) 2019 Paul Wouters <pwouters@redhat.com>
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

#include <limits.h>

#include "lswlog.h"
#include "lswalloc.h"
#include "alg_byname.h"

#include "ike_alg.h"
#include "ike_alg_encrypt.h"
#include "ike_alg_integ.h"
#include "ike_alg_prf.h"
#include "ike_alg_dh.h"
#include "proposals.h"

static bool ike_proposal_ok(struct proposal_parser *parser,
			    const struct proposal *proposal)
{
	if (!proposal_aead_none_ok(parser, proposal)) {
		if (!impair_proposal_errors(parser)) {
			return false;
		}
	}

	/*
	 * Check that the ALG_INFO spec is implemented.
	 */

	impaired_passert(PROPOSAL_PARSER,
			 next_algorithm(proposal, PROPOSAL_encrypt, NULL) != NULL);
	FOR_EACH_ALGORITHM(proposal, encrypt, alg) {
		const struct encrypt_desc *encrypt = encrypt_desc(alg->desc);
		passert(ike_alg_is_ike(&encrypt->common));
		passert(IMPAIR(PROPOSAL_PARSER) ||
			alg->enckeylen == 0 ||
			encrypt_has_key_bit_length(encrypt,
						   alg->enckeylen));
	}

	impaired_passert(PROPOSAL_PARSER,
			 next_algorithm(proposal, PROPOSAL_prf, NULL) != NULL);
	FOR_EACH_ALGORITHM(proposal, prf, alg) {
		const struct prf_desc *prf = prf_desc(alg->desc);
		passert(ike_alg_is_ike(&prf->common));
	}

	impaired_passert(PROPOSAL_PARSER,
			 next_algorithm(proposal, PROPOSAL_integ, NULL) != NULL);
	FOR_EACH_ALGORITHM(proposal, integ, alg) {
		const struct integ_desc *integ = integ_desc(alg->desc);
		passert(integ == &ike_alg_integ_none ||
			ike_alg_is_ike(&integ->common));
	}

	impaired_passert(PROPOSAL_PARSER,
			 next_algorithm(proposal, PROPOSAL_dh, NULL) != NULL);
	FOR_EACH_ALGORITHM(proposal, dh, alg) {
		const struct oakley_group_desc *dh = dh_desc(alg->desc);
		passert(ike_alg_is_ike(&dh->common));
		if (dh == &ike_alg_dh_none) {
			proposal_error(parser, "IKE DH algorithm 'none' not permitted");
			if (!impair_proposal_errors(parser)) {
				return false;
			}
		}
	}

	return true;
}

/*
 * "ike_info" proposals are built built by first parsing the ike=
 * line, and second merging it with the below defaults when an
 * algorithm wasn't specified.
 *
 * Do not assume that these hard wired algorithms are actually valid.
 */

static const struct ike_alg *default_ikev1_groups[] = {
	&oakley_group_modp2048.common,
	&oakley_group_modp1536.common,
	NULL,
};
static const struct ike_alg *default_ikev2_groups[] = {
	&oakley_group_modp2048.common,
	&oakley_group_modp3072.common,
	&oakley_group_modp4096.common,
	&oakley_group_modp8192.common,
	&oakley_group_dh19.common,
	&oakley_group_dh20.common,
	&oakley_group_dh21.common,
#ifdef USE_DH31
	&oakley_group_dh31.common,
#endif
	NULL,
};

/*
 * since ike= must have an encryption algorithm this is normally
 * ignored.
 */
static const struct ike_alg *default_ike_ealgs[] = {
#ifdef USE_AES
	&ike_alg_encrypt_aes_cbc.common,
#endif
#ifdef USE_3DES
	&ike_alg_encrypt_3des_cbc.common,
#endif
	NULL,
};

static const struct ike_alg *default_v1_ike_prfs[] = {
#ifdef USE_SHA2
	&ike_alg_prf_sha2_256.common,
	&ike_alg_prf_sha2_512.common,
#endif
#ifdef USE_SHA1
	&ike_alg_prf_sha1.common,
#endif
	NULL,
};

static const struct ike_alg *default_v2_ike_prfs[] = {
#ifdef USE_SHA2
	&ike_alg_prf_sha2_512.common,
	&ike_alg_prf_sha2_256.common,
#endif
	NULL,
};

const struct proposal_defaults ikev1_ike_defaults = {
	.dh = default_ikev1_groups,
	.encrypt = default_ike_ealgs,
	.prf = default_v1_ike_prfs,
};

const struct proposal_defaults ikev2_ike_defaults = {
	.dh = default_ikev2_groups,
	.encrypt = default_ike_ealgs,
	.prf = default_v2_ike_prfs,
};

const struct proposal_protocol ike_proposal_protocol = {
	.name = "IKE",
	.ikev1_alg_id = IKEv1_OAKLEY_ID,
	.protoid = PROTO_ISAKMP,
	.defaults = {
		[IKEv1] = &ikev1_ike_defaults,
		[IKEv2] = &ikev2_ike_defaults,
	},
	.proposal_ok = ike_proposal_ok,
	.encrypt_alg_byname = encrypt_alg_byname,
	.prf_alg_byname = prf_alg_byname,
	.integ_alg_byname = integ_alg_byname,
	.dh_alg_byname = dh_alg_byname,
};

struct proposal_parser *ike_proposal_parser(const struct proposal_policy *policy)
{
	return alloc_proposal_parser(policy, &ike_proposal_protocol);
}
