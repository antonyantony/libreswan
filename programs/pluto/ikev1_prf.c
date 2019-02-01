/*
 * Calculate IKEv1 prf and keying material, for libreswan
 *
 * Copyright (C) 2007 Michael C. Richardson <mcr@xelerance.com>
 * Copyright (C) 2010 Paul Wouters <paul@xelerance.com>
 * Copyright (C) 2013 D. Hugh Redelmeier <hugh@mimosa.com>
 * Copyright (C) 2015 Andrew Cagney <cagney@gnu.org>
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

#include "ikev1_prf.h"

#include "crypt_prf.h"
#include "crypt_symkey.h"

/*
 * Compute: SKEYID = prf(Ni_b | Nr_b, g^xy)
 *
 * MUST BE THREAD-SAFE
 */
PK11SymKey *ikev1_signature_skeyid(const struct prf_desc *prf_desc,
				   const chunk_t Ni,
				   const chunk_t Nr,
				   /*const*/ PK11SymKey *dh_secret /* NSS doesn't do const */)
{
	/* key = Ni|Nr */
	chunk_t key = clone_chunk_chunk(Ni, Nr, "key = Ni|Nr");
	struct crypt_prf *prf = crypt_prf_init_chunk("SKEYID sig",
						     prf_desc,
						     "Ni|Nr", key);
	freeanychunk(key);
	/* seed = g^xy */
	crypt_prf_update_symkey(prf, "g^xy", dh_secret);
	/* generate */
	return crypt_prf_final_symkey(&prf);
}

/*
 * Compute: SKEYID = prf(pre-shared-key, Ni_b | Nr_b)
 */
PK11SymKey *ikev1_pre_shared_key_skeyid(const struct prf_desc *prf_desc,
					chunk_t pre_shared_key,
					chunk_t Ni, chunk_t Nr)
{
	/* key = pre-shared-key */
	struct crypt_prf *prf = crypt_prf_init_chunk("SKEYID psk", prf_desc,
						     "psk", pre_shared_key);
	/* seed = Ni_b | Nr_b */
	crypt_prf_update_chunk(prf, "Ni", Ni);
	crypt_prf_update_chunk(prf, "Nr", Nr);
	/* generate */
	return crypt_prf_final_symkey(&prf);
}

/*
 * SKEYID_d = prf(SKEYID, g^xy | CKY-I | CKY-R | 0)
 */
PK11SymKey *ikev1_skeyid_d(const struct prf_desc *prf_desc,
			   PK11SymKey *skeyid,
			   PK11SymKey *dh_secret,
			   chunk_t cky_i, chunk_t cky_r)
{
	/* key = SKEYID */
	struct crypt_prf *prf = crypt_prf_init_symkey("SKEYID_d", prf_desc,
						      "SKEYID", skeyid);
	/* seed = g^xy | CKY-I | CKY-R | 0 */
	crypt_prf_update_symkey(prf, "g^xy", dh_secret);
	crypt_prf_update_chunk(prf, "CKI_i", cky_i);
	crypt_prf_update_chunk(prf, "CKI_r", cky_r);
	crypt_prf_update_byte(prf, "0", 0);
	/* generate */
	return crypt_prf_final_symkey(&prf);
}

/*
 * SKEYID_a = prf(SKEYID, SKEYID_d | g^xy | CKY-I | CKY-R | 1)
 */
PK11SymKey *ikev1_skeyid_a(const struct prf_desc *prf_desc,
			   PK11SymKey *skeyid,
			   PK11SymKey *skeyid_d, PK11SymKey *dh_secret,
			   chunk_t cky_i, chunk_t cky_r)
{
	/* key = SKEYID */
	struct crypt_prf *prf = crypt_prf_init_symkey("SKEYID_a", prf_desc,
						      "SKEYID", skeyid);
	/* seed = SKEYID_d | g^xy | CKY-I | CKY-R | 1 */
	crypt_prf_update_symkey(prf, "SKEYID_d", skeyid_d);
	crypt_prf_update_symkey(prf, "g^xy", dh_secret);
	crypt_prf_update_chunk(prf, "CKI_i", cky_i);
	crypt_prf_update_chunk(prf, "CKI_r", cky_r);
	crypt_prf_update_byte(prf, "1", 1);
	/* generate */
	return crypt_prf_final_symkey(&prf);
}

/*
 * SKEYID_e = prf(SKEYID, SKEYID_a | g^xy | CKY-I | CKY-R | 2)
 */
PK11SymKey *ikev1_skeyid_e(const struct prf_desc *prf_desc,
			   PK11SymKey *skeyid,
			   PK11SymKey *skeyid_a, PK11SymKey *dh_secret,
			   chunk_t cky_i, chunk_t cky_r)
{
	/* key = SKEYID */
	struct crypt_prf *prf = crypt_prf_init_symkey("SKEYID_e", prf_desc,
						      "SKEYID", skeyid);
	/* seed = SKEYID_a | g^xy | CKY-I | CKY-R | 2 */
	crypt_prf_update_symkey(prf, "SKEYID_a", skeyid_a);
	crypt_prf_update_symkey(prf, "g^xy", dh_secret);
	crypt_prf_update_chunk(prf, "CKI_i", cky_i);
	crypt_prf_update_chunk(prf, "CKI_r", cky_r);
	crypt_prf_update_byte(prf, "2", 2);
	/* generate */
	return crypt_prf_final_symkey(&prf);
}

PK11SymKey *appendix_b_keymat_e(const struct prf_desc *prf_desc,
				const struct encrypt_desc *encrypter,
				PK11SymKey *skeyid_e,
				unsigned required_keymat)
{
	if (sizeof_symkey(skeyid_e) >= required_keymat) {
		return encrypt_key_from_symkey_bytes("keymat", encrypter,
						     0, required_keymat,
						     skeyid_e);
	}
	/* K1 = prf(skeyid_e, 0) */
	PK11SymKey *keymat;
	{
		struct crypt_prf *prf = crypt_prf_init_symkey("appendix_b", prf_desc,
							      "SKEYID_e", skeyid_e);
		crypt_prf_update_byte(prf, "0", 0);
		keymat = crypt_prf_final_symkey(&prf);
	}

	/* make a reference to keep things easy */
	PK11SymKey *old_k = reference_symkey(__func__, "old_k#1", keymat);
	while (sizeof_symkey(keymat) < required_keymat) {
		/* Kn = prf(skeyid_e, Kn-1) */
		struct crypt_prf *prf = crypt_prf_init_symkey("Kn", prf_desc,
							      "SKEYID_e", skeyid_e);
		crypt_prf_update_symkey(prf, "old_k", old_k);
		PK11SymKey *new_k = crypt_prf_final_symkey(&prf);
		append_symkey_symkey(&keymat, new_k);
		release_symkey(__func__, "old_k#N", &old_k);
		old_k = new_k;
	}
	release_symkey(__func__, "old_k#final", &old_k);
	PK11SymKey *cryptkey = encrypt_key_from_symkey_bytes("cryptkey", encrypter,
							     0, required_keymat,
							     keymat);
	release_symkey(__func__, "keymat", &keymat);
	return cryptkey;
}
