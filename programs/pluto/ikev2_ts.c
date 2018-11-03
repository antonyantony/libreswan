/* IKEv2 Traffic Selectors, for libreswan
 *
 * Copyright (C) 2007-2008 Michael Richardson <mcr@xelerance.com>
 * Copyright (C) 2009-2010 Paul Wouters <paul@xelerance.com>
 * Copyright (C) 2010 Tuomo Soini <tis@foobar.fi>
 * Copyright (C) 2011-2012 Avesh Agarwal <avagarwa@redhat.com>
 * Copyright (C) 2012-2018 Paul Wouters <pwouters@redhat.com>
 * Copyright (C) 2012,2016-2017 Antony Antony <appu@phenome.org>
 * Copyright (C) 2013 D. Hugh Redelmeier <hugh@mimosa.com>
 * Copyright (C) 2014-2015 Andrew cagney <cagney@gnu.org>
 * Copyright (C) 2017 Antony Antony <antony@phenome.org>
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

#include "lswlog.h"

#include "defs.h"
#include "ikev2_ts.h"
#include "connections.h"	/* for struct end */
#include "demux.h"
#include "virtual.h"
#include "hostpair.h"
#include "ikev2.h"		/* for v2_msg_role() */

/*
 * While the RFC seems to suggest that the traffic selectors come in
 * pairs, strongswan, at least, doesn't.
 */
struct traffic_selectors {
	unsigned nr;
	/* ??? is 16 an undocumented limit - IKEv2 has no limit */
	struct traffic_selector ts[16];
};

struct ends {
	const struct end *i;
	const struct end *r;
};

enum narrowing {
	END_EQUALS_TS = 1,
	END_NARROWER_THAN_TS,
	END_WIDER_THAN_TS,
};

static const char *narrowing_string(enum narrowing narrowing)
{
	switch (narrowing) {
	case END_EQUALS_TS: return "==";
	case END_NARROWER_THAN_TS: return "(end)<=(TS)";
	case END_WIDER_THAN_TS: return "(end)>=(TS)";
	default: bad_case(narrowing);
	}
}

void ikev2_print_ts(const struct traffic_selector *ts)
{
	DBG(DBG_CONTROLMORE, {
		char b[RANGETOT_BUF];

		rangetot(&ts->net, 0, b, sizeof(b));
		DBG_log("printing contents struct traffic_selector");
		DBG_log("  ts_type: %s", enum_name(&ikev2_ts_type_names, ts->ts_type));
		DBG_log("  ipprotoid: %d", ts->ipprotoid);
		DBG_log("  port range: %d-%d", ts->startport, ts->endport);
		DBG_log("  ip range: %s", b);
	});
}

/* rewrite me with addrbytesptr_write() */
struct traffic_selector ikev2_end_to_ts(const struct end *e)
{
	struct traffic_selector ts;

	zero(&ts);	/* OK: no pointer fields */

	/* subnet => range */
	ts.net.start = e->client.addr;
	ts.net.end = e->client.addr;
	switch (addrtypeof(&e->client.addr)) {
	case AF_INET:
	{
		struct in_addr v4mask = bitstomask(e->client.maskbits);

		ts.ts_type = IKEv2_TS_IPV4_ADDR_RANGE;
		ts.net.start.u.v4.sin_addr.s_addr &= v4mask.s_addr;
		ts.net.end.u.v4.sin_addr.s_addr |= ~v4mask.s_addr;
		break;
	}
	case AF_INET6:
	{
		struct in6_addr v6mask = bitstomask6(e->client.maskbits);

		ts.ts_type = IKEv2_TS_IPV6_ADDR_RANGE;
		ts.net.start.u.v6.sin6_addr.s6_addr32[0] &= v6mask.s6_addr32[0];
		ts.net.start.u.v6.sin6_addr.s6_addr32[1] &= v6mask.s6_addr32[1];
		ts.net.start.u.v6.sin6_addr.s6_addr32[2] &= v6mask.s6_addr32[2];
		ts.net.start.u.v6.sin6_addr.s6_addr32[3] &= v6mask.s6_addr32[3];

		ts.net.end.u.v6.sin6_addr.s6_addr32[0] |= ~v6mask.s6_addr32[0];
		ts.net.end.u.v6.sin6_addr.s6_addr32[1] |= ~v6mask.s6_addr32[1];
		ts.net.end.u.v6.sin6_addr.s6_addr32[2] |= ~v6mask.s6_addr32[2];
		ts.net.end.u.v6.sin6_addr.s6_addr32[3] |= ~v6mask.s6_addr32[3];
		break;
	}

	}
	/* Setting ts_type IKEv2_TS_FC_ADDR_RANGE (RFC-4595) not yet supported */

	ts.ipprotoid = e->protocol;

	/*
	 * if port is %any or 0 we mean all ports (or all iccmp/icmpv6)
	 * See RFC-5996 Section 3.13.1 handling for ICMP(1) and ICMPv6(58)
	 *   we only support providing Type, not Code, eg protoport=1/1
	 */
	if (e->port == 0 || e->has_port_wildcard) {
		ts.startport = 0;
		ts.endport = 65535;
	} else {
		ts.startport = e->port;
		ts.endport = e->port;
	}

	return ts;
}

static stf_status ikev2_emit_ts(pb_stream *outpbs,
				const struct_desc *ts_desc,
				const struct traffic_selector *ts)
{
	pb_stream ts_pbs;

	{
		struct ikev2_ts its = {
			.isat_critical = ISAKMP_PAYLOAD_NONCRITICAL,
			.isat_num = 1,
		};

		if (!out_struct(&its, ts_desc, outpbs, &ts_pbs))
			return STF_INTERNAL_ERROR;
	}

	pb_stream ts_pbs2;

	{
		struct ikev2_ts1 its1 = {
			.isat1_ipprotoid = ts->ipprotoid,   /* protocol as per local policy */
			.isat1_startport = ts->startport,   /* ports as per local policy */
			.isat1_endport = ts->endport,
		};
		switch (ts->ts_type) {
		case IKEv2_TS_IPV4_ADDR_RANGE:
			its1.isat1_type = IKEv2_TS_IPV4_ADDR_RANGE;
			its1.isat1_sellen = 2 * 4 + 8; /* See RFC 5669 SEction 13.3.1, 8 octet header plus 2 ip addresses */
			break;
		case IKEv2_TS_IPV6_ADDR_RANGE:
			its1.isat1_type = IKEv2_TS_IPV6_ADDR_RANGE;
			its1.isat1_sellen = 2 * 16 + 8; /* See RFC 5669 SEction 13.3.1, 8 octet header plus 2 ip addresses */
			break;
		case IKEv2_TS_FC_ADDR_RANGE:
			DBG_log("IKEv2 Traffic Selector IKEv2_TS_FC_ADDR_RANGE not yet supported");
			return STF_INTERNAL_ERROR;

		default:
			DBG_log("IKEv2 Traffic Selector type '%d' not supported",
				ts->ts_type);
		}

		if (!out_struct(&its1, &ikev2_ts1_desc, &ts_pbs, &ts_pbs2))
			return STF_INTERNAL_ERROR;
	}

	/* now do IP addresses */
	switch (ts->ts_type) {
	case IKEv2_TS_IPV4_ADDR_RANGE:
		if (!out_raw(&ts->net.start.u.v4.sin_addr.s_addr, 4, &ts_pbs2,
			     "ipv4 start") ||
		    !out_raw(&ts->net.end.u.v4.sin_addr.s_addr, 4, &ts_pbs2,
			     "ipv4 end"))
			return STF_INTERNAL_ERROR;

		break;
	case IKEv2_TS_IPV6_ADDR_RANGE:
		if (!out_raw(&ts->net.start.u.v6.sin6_addr.s6_addr, 16, &ts_pbs2,
			     "ipv6 start") ||
		    !out_raw(&ts->net.end.u.v6.sin6_addr.s6_addr, 16, &ts_pbs2,
			     "ipv6 end"))
			return STF_INTERNAL_ERROR;

		break;
	case IKEv2_TS_FC_ADDR_RANGE:
		DBG_log("Traffic Selector IKEv2_TS_FC_ADDR_RANGE not supported");
		return STF_FAIL;

	default:
		DBG_log("Failed to create unknown IKEv2 Traffic Selector payload '%d'",
			ts->ts_type);
		return STF_FAIL;
	}

	close_output_pbs(&ts_pbs2);
	close_output_pbs(&ts_pbs);

	return STF_OK;
}

stf_status v2_emit_ts_payloads(const struct child_sa *child,
			       pb_stream *outpbs,
			       const struct connection *c0)
{
	const struct traffic_selector *ts_i, *ts_r;

	switch (child->sa.st_sa_role) {
	case SA_INITIATOR:
		ts_i = &child->sa.st_ts_this;
		ts_r = &child->sa.st_ts_that;
		break;
	case SA_RESPONDER:
		ts_i = &child->sa.st_ts_that;
		ts_r = &child->sa.st_ts_this;
		break;
	default:
		bad_case(child->sa.st_sa_role);
	}

	/*
	 * XXX: this looks wrong
	 *
	 * - instead of emitting two traffic selector payloads (TSi
	 *   TSr) each containg all the corresponding traffic
	 *   selectors, it is emitting a sequence of traffic selector
	 *   payloads each containg just one traffic selector
	 *
	 * - should multiple initiator (responder) traffic selector
	 *   payloads be emitted then they will all contain the same
	 *   value - the loop control variable SR is never referenced
	 *
	 * - should multiple traffic selector payload be emitted then
	 *   the next payload type for all but the last v2TSr payload
	 *   will be wrong - it is always set to the type of the
	 *   payload after these
	 */

	for (const struct spd_route *sr = &c0->spd; sr != NULL;
	     sr = sr->spd_next) {
		stf_status ret = ikev2_emit_ts(outpbs, &ikev2_ts_i_desc, ts_i);

		if (ret != STF_OK)
			return ret;
		ret = ikev2_emit_ts(outpbs, &ikev2_ts_r_desc, ts_r);
		if (ret != STF_OK)
			return ret;
	}

	return STF_OK;
}

/* return number of traffic selectors found; -1 for error */
static bool v2_parse_ts(const char *role,
			struct payload_digest *const ts_pd,
			struct traffic_selectors *tss)
{
	DBGF(DBG_MASK, "TS: parsing %u %s traffic selectors",
	     ts_pd->payload.v2ts.isat_num, role);

	if (ts_pd->payload.v2ts.isat_num >= elemsof(tss->ts)) {
		libreswan_log("TS contains %d entries which exceeds hardwired max of %zu",
			      ts_pd->payload.v2ts.isat_num, elemsof(tss->ts));
		return false;	/* won't fit in array */
	}

	for (tss->nr = 0; tss->nr < ts_pd->payload.v2ts.isat_num; tss->nr++) {
		struct traffic_selector *ts = &tss->ts[tss->nr];

		pb_stream addr;
		struct ikev2_ts1 ts1;
		if (!in_struct(&ts1, &ikev2_ts1_desc, &ts_pd->pbs, &addr))
			return false;

		switch (ts1.isat1_type) {
		case IKEv2_TS_IPV4_ADDR_RANGE:
			ts->ts_type = IKEv2_TS_IPV4_ADDR_RANGE;
			SET_V4(ts->net.start);
			if (!in_raw(&ts->net.start.u.v4.sin_addr.s_addr,
				    sizeof(ts->net.start.u.v4.sin_addr.s_addr),
				    &addr, "ipv4 ts low"))
				return false;

			SET_V4(ts->net.end);

			if (!in_raw(&ts->net.end.u.v4.sin_addr.s_addr,
				    sizeof(ts->net.end.u.v4.sin_addr.s_addr),
				    &addr, "ipv4 ts high"))
				return false;

			break;

		case IKEv2_TS_IPV6_ADDR_RANGE:
			ts->ts_type = IKEv2_TS_IPV6_ADDR_RANGE;
			SET_V6(ts->net.start);

			if (!in_raw(&ts->net.start.u.v6.sin6_addr.s6_addr,
				    sizeof(ts->net.start.u.v6.sin6_addr.s6_addr),
				    &addr, "ipv6 ts low"))
				return false;

			SET_V6(ts->net.end);

			if (!in_raw(&ts->net.end.u.v6.sin6_addr.s6_addr,
				    sizeof(ts->net.end.u.v6.sin6_addr.s6_addr),
				    &addr, "ipv6 ts high"))
				return false;

			break;

		default:
			return false;
		}

		if (pbs_left(&addr) != 0)
			return false;

		ts->ipprotoid = ts1.isat1_ipprotoid;

		ts->startport = ts1.isat1_startport;
		ts->endport = ts1.isat1_endport;
		if (ts->startport > ts->endport) {
			libreswan_log("%s traffic selector %d has an invalid port range",
				      role, tss->nr);
			return false;
		}
	}

	DBGF(DBG_MASK, "TS: parsed %d %s TS payloads", tss->nr, role);
	return true;
}

static bool v2_parse_tss(const struct msg_digest *md,
			 struct traffic_selectors *tsi,
			 struct traffic_selectors *tsr)
{
	if (!v2_parse_ts("initiator", md->chain[ISAKMP_NEXT_v2TSi], tsi)) {
		return false;
	}

	if (!v2_parse_ts("responder", md->chain[ISAKMP_NEXT_v2TSr], tsr)) {
		return false;
	}

	return true;
}

#define MATCH_PREFIX "        "

/*
 * Check if our policy's protocol (proto) matches the Traffic Selector
 * protocol (ts_proto).
 */

static int ikev2_match_protocol(const struct end *end,
				const struct traffic_selector *ts,
				enum narrowing narrowing,
				const char *which, int index)
{
	int f = 0;	/* strength of match */
	const char *m = "no";

	switch (narrowing) {
	case END_EQUALS_TS:
		if (end->protocol == ts->ipprotoid) {
			f = 255;	/* ??? odd value */
			m = "exact";
		}
		break;
	case END_NARROWER_THAN_TS:
		if (ts->ipprotoid == 0) { /* wild-card */
			f = 1;
			m = "superset";
		}
		break;
	case END_WIDER_THAN_TS:
		if (end->protocol == 0) { /* wild-card */
			f = 1;
			m = "subset";
		}
		break;
	default:
		bad_case(narrowing);
	}
	DBGF(DBG_MASK, MATCH_PREFIX "protocol %s%d %s %s[%d].ipprotoid %s%d: %s fitness %d",
	     end->protocol == 0 ? "*" : "", end->protocol,
	     narrowing_string(narrowing),
	     which, index, ts->ipprotoid == 0 ? "*" : "", ts->ipprotoid,
	     m, f);
	return f;
}

/*
 * returns -1 on no match; otherwise a weight of how great the match was.
 * *best_tsi_i and *best_tsr_i are set if there was a match.
 * Almost identical to ikev2_evaluate_connection_port_fit:
 * any change should be done to both.
 */
static int ikev2_evaluate_connection_protocol_fit(enum narrowing narrowing,
						  const struct ends *end,
						  const struct traffic_selectors *tsi,
						  const struct traffic_selectors *tsr,
						  int *best_tsi_i,
						  int *best_tsr_i)
{
	int bestfit_pr = -1;

	/* compare tsi/r array to this/that, evaluating protocol how well it fits */
	/* ??? stupid n**2 algorithm */
	for (unsigned tsi_ni = 0; tsi_ni < tsi->nr; tsi_ni++) {
		const struct traffic_selector *tni = &tsi->ts[tsi_ni];

		int fitrange_i = ikev2_match_protocol(end->i, tni, narrowing,
						      "TSi", tsi_ni);

		if (fitrange_i == 0)
			continue;	/* save effort! */

		for (unsigned tsr_ni = 0; tsr_ni < tsr->nr; tsr_ni++) {
			const struct traffic_selector *tnr = &tsr->ts[tsr_ni];

			int fitrange_r = ikev2_match_protocol(end->r, tnr, narrowing,
							      "TSr", tsr_ni);

			if (fitrange_r == 0)
				continue;	/* save effort! */

			int matchiness = fitrange_i + fitrange_r;	/* ??? arbitrary objective function */

			if (matchiness > bestfit_pr) {
				*best_tsi_i = tsi_ni;
				*best_tsr_i = tsr_ni;
				bestfit_pr = matchiness;
				DBG(DBG_CONTROL,
				    DBG_log("    best protocol fit so far: tsi[%d] fitrange_i %d, tsr[%d] fitrange_r %d, matchiness %d",
					    *best_tsi_i, fitrange_i,
					    *best_tsr_i, fitrange_r,
					    matchiness));
			}
		}
	}
	DBG(DBG_CONTROL, DBG_log("    protocol_fitness %d", bestfit_pr));
	return bestfit_pr;
}


/*
 * Check if our policy's port (port) matches
 * the Traffic Selector port range (ts.startport to ts.endport)
 * Note port == 0 means port range 0 to 65535.
 * If superset_ok, narrowing ts port range to our port range is OK (responder narrowing)
 * If subset_ok, narrowing our port range to ts port range is OK (initiator narrowing).
 * Returns 0 if no match; otherwise number of ports within match
 */
static int ikev2_match_port_range(const struct end *end,
				  const struct traffic_selector *ts,
				  enum narrowing narrowing,
				  const char *which, int index)
{
	uint16_t end_low = end->port;
	uint16_t end_high = end->port == 0 ? 65535 : end->port;
	int f = 0;	/* strength of match */
	const char *m = "no";

	switch (narrowing) {
	case END_EQUALS_TS:
		if (end_low == ts->startport && end_high == ts->endport) {
			f = 1 + (end_high - end_low);
		}
		break;
	case END_NARROWER_THAN_TS:
		if (end_low >= ts->startport && end_high <= ts->endport) {
			f = 1 + (end_high - end_low);
		}
		break;
	case END_WIDER_THAN_TS:
		if (end_low <= ts->startport && end_high >= ts->endport) {
			f = 1 + (ts->endport - ts->startport);
		}
		break;
	default:
		bad_case(narrowing);
	}
	DBGF(DBG_MASK, MATCH_PREFIX "port %u..%u %s %s[%d] %u..%u: %s fitness %d",
	     end_low, end_high,
	     narrowing_string(narrowing),
	     which, index, ts->startport, ts->endport,
	     m, f);
	return f;
}

/*
 * returns -1 on no match; otherwise a weight of how great the match was.
 * *best_tsi_i and *best_tsr_i are set if there was a match.
 * Almost identical to ikev2_evaluate_connection_protocol_fit:
 * any change should be done to both.
 */
static int ikev2_evaluate_connection_port_fit(enum narrowing narrowing,
					      const struct ends *end,
					      const struct traffic_selectors *tsi,
					      const struct traffic_selectors *tsr,
					      int *best_tsi_i,
					      int *best_tsr_i)
{
	int bestfit_p = -1;

	/* compare tsi/r array to this/that, evaluating how well each port range fits */
	/* ??? stupid n**2 algorithm */
	for (unsigned tsi_ni = 0; tsi_ni < tsi->nr; tsi_ni++) {
		const struct traffic_selector *tni = &tsi->ts[tsi_ni];

		int fitrange_i = ikev2_match_port_range(end->i, tni, narrowing,
							"TSi", tsi_ni);

		if (fitrange_i == 0)
			continue;	/* save effort! */

		for (unsigned tsr_ni = 0; tsr_ni < tsr->nr; tsr_ni++) {
			const struct traffic_selector *tnr = &tsr->ts[tsr_ni];

			int fitrange_r = ikev2_match_port_range(end->r, tnr, narrowing,
								"TSr", tsr_ni);

			if (fitrange_r == 0)
				continue;	/* no match */

			int matchiness = fitrange_i + fitrange_r;	/* ??? arbitrary objective function */

			if (matchiness > bestfit_p) {
				*best_tsi_i = tsi_ni;
				*best_tsr_i = tsr_ni;
				bestfit_p = matchiness;
				DBG(DBG_CONTROL,
				    DBG_log("    best ports fit so far: tsi[%d] fitrange_i %d, tsr[%d] fitrange_r %d, matchiness %d",
					    *best_tsi_i, fitrange_i,
					    *best_tsr_i, fitrange_r,
					    matchiness));
			}
		}
	}
	DBG(DBG_CONTROL, DBG_log("    port_fitness %d", bestfit_p));
	return bestfit_p;
}

/*
 * Does TS fit inside of END?
 *
 * Given other code flips the comparison depending initiator or
 * responder, is this right?
 *
 * NOTE: Our parser/config only allows 1 CIDR, however IKEv2 ranges
 *       can be non-CIDR for now we really support/limit ourselves to
 *       a single CIDR
 *
 * XXX: what exactly is CIDR?
 */

static int match_address_range(const struct end *end,
			       const struct traffic_selector *ts,
			       enum narrowing narrowing,
			       const char *which, int index)
{
	/*
	 * Pre-compute possible fit --- sum of bits gives how good a
	 * fit this is.
	 */
	int ts_range = iprange_bits(ts->net.start, ts->net.end);
	int maskbits = end->client.maskbits;
	int fitbits = maskbits + ts_range;

	int f = 0;
	const char *m = "no";

	/*
	 * NOTE: Our parser/config only allows 1 CIDR, however IKEv2
	 *       ranges can be non-CIDR for now we really
	 *       support/limit ourselves to a single CIDR
	 *
	 * XXX: so what is CIDR?
	 */
	switch (narrowing) {
	case END_EQUALS_TS:
	case END_NARROWER_THAN_TS:
		PASSERT_FAIL("%s", "what should happen here?");
	case END_WIDER_THAN_TS:
		/* i.e., TS <= END */
		if (addrinsubnet(&ts->net.start, &end->client) &&
		    addrinsubnet(&ts->net.end, &end->client)) {
			m = "yes";
			f = fitbits;
		}
		break;
	default:
		bad_case(narrowing);
	}

	/*
	 * comparing for ports for finding better local policy
	 *
	 * XXX: why do this?
	 */
	/* ??? arbitrary modification to objective function */
	DBGF(DBG_MASK, MATCH_PREFIX "end->port %d ts->startport %d ts->endport %d",
	     end->port, ts->startport, ts->endport);
	if (end->port != 0 &&
	    ts->startport == end->port &&
	    ts->endport == end->port)
		f = f << 1;

	DBGF(DBG_MASK, MATCH_PREFIX "maskbits=%u addr=? %s %s[%u] ts_range=%u: %s fitness %d",
	     maskbits, narrowing_string(narrowing),
	     which, index, ts_range, m, f);
	return f;
}

/*
 * RFC 5996 section 2.9 "Traffic Selector Negotiation"
 * Future: section 2.19 "Requesting an Internal Address on a Remote Network"
 */
static int ikev2_evaluate_connection_fit(const struct connection *d,
					 const struct ends *e,
					 const struct traffic_selectors *tsi,
					 const struct traffic_selectors *tsr)
{
	int bestfit = -1;

	DBG(DBG_CONTROLMORE, {
		char ei3[SUBNETTOT_BUF];
		char er3[SUBNETTOT_BUF];
		char cib[CONN_INST_BUF];
		subnettot(&e->i->client,  0, ei3, sizeof(ei3));
		subnettot(&e->r->client,  0, er3, sizeof(er3));
		DBG_log("  ikev2_evaluate_connection_fit evaluating our conn=\"%s\"%s I=%s:%d/%d R=%s:%d/%d %s to their:",
			d->name, fmt_conn_instance(d, cib),
			ei3, e->i->protocol, e->i->port,
			er3, e->r->protocol, e->r->port,
			is_virtual_connection(d) ? "(virt)" : "");
	});

	/* compare tsi/r array to this/that, evaluating how well it fits */
	for (unsigned tsi_ni = 0; tsi_ni < tsi->nr; tsi_ni++) {
		const struct traffic_selector *tni = &tsi->ts[tsi_ni];

		/* choice hardwired! */
		int fit_i = match_address_range(e->i, tni,
						END_WIDER_THAN_TS,
						"TSi", tsi_ni);
		if (fit_i <= 0) {
			continue;
		}

		for (unsigned tsr_ni = 0; tsr_ni < tsr->nr; tsr_ni++) {
			const struct traffic_selector *tnr = &tsr->ts[tsr_ni];

			/* do addresses fit into the policy? */

			/* choice hardwired! */
			int fit_r = match_address_range(e->r, tnr,
							END_WIDER_THAN_TS,
							"TSr", tsr_ni);
			if (fit_r <= 0) {
				continue;
			}

			/* ??? this objective function is odd and arbitrary */
			int fitbits = (fit_i << 8) + fit_r;

			if (fitbits > bestfit)
				bestfit = fitbits;
		}
	}

	return bestfit;
}

/*
 * find the best connection and, if it is AUTH exchange, create the
 * child state
 *
 * XXX: creating child as a side effect is pretty messed up.
 */
bool v2_process_ts_request(struct child_sa *child,
			   const struct msg_digest *md)
{
	passert(v2_msg_role(md) == MESSAGE_REQUEST);
	passert(child->sa.st_sa_role == SA_RESPONDER);

	/* XXX: md->st here is parent???? */
	struct connection *c = md->st->st_connection;

	struct traffic_selectors tsi = { .nr = 0, };
	struct traffic_selectors tsr = { .nr = 0, };
	if (!v2_parse_tss(md, &tsi, &tsr)) {
		return false;
	}

	/* best so far */
	int bestfit_n = -1;
	int bestfit_p = -1;
	int bestfit_pr = -1;
	const struct spd_route *bsr = NULL;	/* best spd_route so far */

	int best_tsi_i = -1;
	int best_tsr_i = -1;

	/* find best spd in c */
	const struct spd_route *sra;

	for (sra = &c->spd; sra != NULL; sra = sra->spd_next) {

		/* responder */
		const struct ends e = {
			.i = &sra->that,
			.r = &sra->this,
		};

		int bfit_n = ikev2_evaluate_connection_fit(c, &e, &tsi, &tsr);

		if (bfit_n > bestfit_n) {
			DBG(DBG_CONTROLMORE,
			    DBG_log("prefix fitness found a better match c %s",
				    c->name));

			/* responder */
			enum narrowing responder_narrowing =
				(c->policy & POLICY_IKEV2_ALLOW_NARROWING)
				? END_NARROWER_THAN_TS
				: END_EQUALS_TS;
			int bfit_p = ikev2_evaluate_connection_port_fit(responder_narrowing,
									&e, &tsi, &tsr,
									&best_tsi_i,
									&best_tsr_i);

			if (bfit_p > bestfit_p) {
				DBG(DBG_CONTROLMORE,
				    DBG_log("port fitness found better match c %s, tsi[%d],tsr[%d]",
					    c->name, best_tsi_i, best_tsr_i));
				int bfit_pr =
					ikev2_evaluate_connection_protocol_fit(responder_narrowing,
									       &e, &tsi, &tsr,
									       &best_tsi_i,
									       &best_tsr_i);

				if (bfit_pr > bestfit_pr) {
					DBG(DBG_CONTROLMORE,
					    DBG_log("protocol fitness found better match c %s, tsi[%d],tsr[%d]",
						    c->name,
						    best_tsi_i,
						    best_tsr_i));

					bestfit_p = bfit_p;
					bestfit_n = bfit_n;
					bsr = sra;
				} else {
					DBG(DBG_CONTROLMORE,
					    DBG_log("protocol fitness rejected c %s c->name",
						    c->name));
				}
			} else {
				DBG(DBG_CONTROLMORE,
						DBG_log("port fitness rejected c %s c->name", c->name));
			}
		} else {
			DBG(DBG_CONTROLMORE,
			    DBG_log("prefix fitness rejected c %s c->name", c->name));
		}
	}

	/*
	 * ??? the use of hp looks nonsensical.
	 * Either the first non-empty host_pair should be used
	 * (like the current code) and the following should
	 * be broken into two loops: first find the non-empty
	 * host_pair list, second look through the host_pair list.
	 * OR
	 * what's really meant is look at the host_pair for
	 * each sra, something that matches the current
	 * nested loop structure but not what it actually does.
	 */

	struct connection *best = c;	/* best connection so far */
	const struct host_pair *hp = NULL;

	for (sra = &c->spd; hp == NULL && sra != NULL;
	     sra = sra->spd_next)
	{
		hp = find_host_pair(&sra->this.host_addr,
				    sra->this.host_port,
				    &sra->that.host_addr,
				    sra->that.host_port);

		DBG(DBG_CONTROLMORE, {
			char s2[SUBNETTOT_BUF];
			char d2[SUBNETTOT_BUF];

			subnettot(&sra->this.client, 0, s2,
				  sizeof(s2));
			subnettot(&sra->that.client, 0, d2,
				  sizeof(d2));

			DBG_log("  checking hostpair %s -> %s is %s",
				s2, d2,
				hp == NULL ? "not found" : "found");
		});

		if (hp == NULL)
			continue;

		struct connection *d;

		for (d = hp->connections; d != NULL; d = d->hp_next) {
			/* groups are templates instantiated as GROUPINSTANCE */
			if (d->policy & POLICY_GROUP)
				continue;

			/*
			 * ??? same_id && match_id seems redundant.
			 * if d->spd.this.id.kind == ID_NONE, both TRUE
			 * else if c->spd.this.id.kind == ID_NONE,
			 *     same_id treats it as a wildcard and match_id
			 *     does not.  Odd.
			 * else if kinds differ, match_id FALSE
			 * else if kind ID_DER_ASN1_DN, wildcards are forbidden by same_id
			 * else match_id just calls same_id.
			 * So: if wildcards are desired, just use match_id.
			 * If they are not, just use same_id
			 */
			int wildcards;	/* value ignored */
			int pathlen;	/* value ignored */
			if (!(same_id(&c->spd.this.id,
				      &d->spd.this.id) &&
			      match_id(&c->spd.that.id,
				       &d->spd.that.id, &wildcards) &&
			      trusted_ca_nss(c->spd.that.ca,
					 d->spd.that.ca, &pathlen)))
			{
				DBG(DBG_CONTROLMORE, DBG_log("connection \"%s\" does not match IDs or CA of current connection \"%s\"",
					d->name, c->name));
				continue;
			}
			DBG(DBG_CONTROLMORE, DBG_log("investigating connection \"%s\" as a better match", d->name));

			const struct spd_route *sr;

			for (sr = &d->spd; sr != NULL; sr = sr->spd_next) {

				/* responder */
				const struct ends e = {
					.i = &sr->that,
					.r = &sr->this,
				};

				int newfit = ikev2_evaluate_connection_fit(d, &e, &tsi, &tsr);

				if (newfit > bestfit_n) {
					/* ??? what does this comment mean? */
					/* will complicated this with narrowing */
					DBG(DBG_CONTROLMORE,
					    DBG_log("prefix fitness found a better match d %s",
						    d->name));
					/* responder -- note D! */
					enum narrowing responder_narrowing =
						(d->policy & POLICY_IKEV2_ALLOW_NARROWING)
						? END_NARROWER_THAN_TS
						: END_EQUALS_TS;
					int bfit_p =
						ikev2_evaluate_connection_port_fit(responder_narrowing,
										   &e, &tsi, &tsr,
										   &best_tsi_i,
										   &best_tsr_i);

					if (bfit_p > bestfit_p) {
						DBG(DBG_CONTROLMORE, DBG_log(
							    "port fitness found better match d %s, tsi[%d],tsr[%d]",
							    d->name,
							    best_tsi_i,
							    best_tsr_i));
						int bfit_pr =
							ikev2_evaluate_connection_protocol_fit(responder_narrowing,
											       &e, &tsi, &tsr,
											       &best_tsi_i,
											       &best_tsr_i);

						if (bfit_pr > bestfit_pr) {
							DBG(DBG_CONTROLMORE,
							    DBG_log("protocol fitness found better match d %s, tsi[%d],tsr[%d]",
								    d->name,
								    best_tsi_i,
								    best_tsr_i));

							bestfit_p = bfit_p;
							bestfit_n = newfit;
							best = d;
							bsr = sr;
						} else {
							DBG(DBG_CONTROLMORE,
							    DBG_log("protocol fitness rejected d %s",
								    d->name));
						}
					} else {
						DBG(DBG_CONTROLMORE,
							DBG_log("port fitness rejected d %s",
								d->name));
					}

				} else {
					DBG(DBG_CONTROLMORE,
					    DBG_log("prefix fitness rejected d %s",
						    d->name));
				}
			}
		}
	}

	if (best == c) {
		DBG(DBG_CONTROLMORE, DBG_log("we did not switch connection"));
	}

	if (bsr == NULL) {
		DBG(DBG_CONTROLMORE, DBG_log("failed to find anything; can we instantiate another template?"));

		for (struct connection *t = connections; t != NULL; t = t->ac_next) {
			if (LIN(POLICY_GROUPINSTANCE, t->policy) && (t->kind == CK_TEMPLATE)) {
				/* ??? clang 6.0.0 thinks best might be NULL but I don't see how */
				if (!streq(t->foodgroup, best->foodgroup) ||
				    streq(best->name, t->name) ||
				    !subnetinsubnet(&best->spd.that.client, &t->spd.that.client) ||
				    !sameaddr(&best->spd.this.client.addr, &t->spd.this.client.addr))
					continue;

				/* ??? why require best->name and t->name to be different */

				DBG(DBG_CONTROLMORE,
					DBG_log("investigate %s which is another group instance of %s with different protoports",
						t->name, t->foodgroup));
				/*
				 * ??? this code seems to assume that
				 * tsi and tsr contain exactly one
				 * element.  Any fewer and the code
				 * references an uninitialized value.
				 * Any more would be ignored, and
				 * that's surely wrong.  It would be
				 * nice if the purpose of this block
				 * of code were documented.
				 */
				pexpect(tsi.nr == 1);
				int t_sport =
					tsi.ts[0].startport == tsi.ts[0].endport ? tsi.ts[0].startport :
					tsi.ts[0].startport == 0 && tsi.ts[0].endport == 65535 ? 0 : -1;
				pexpect(tsr.nr == 1);
				int t_dport =
					tsr.ts[0].startport == tsr.ts[0].endport ? tsr.ts[0].startport :
					tsr.ts[0].startport == 0 && tsr.ts[0].endport == 65535 ? 0 : -1;

				if (t_sport == -1 || t_dport == -1)
					continue;

				if ((t->spd.that.protocol != tsi.ts[0].ipprotoid) ||
					(best->spd.this.port != t_sport) ||
					(best->spd.that.port != t_dport))
						continue;

				DBG(DBG_CONTROLMORE, DBG_log("updating connection of group instance for protoports"));
				best->spd.that.protocol = t->spd.that.protocol;
				best->spd.this.port = t->spd.this.port;
				best->spd.that.port = t->spd.that.port;
				pfreeany(best->name);
				best->name = clone_str(t->name, "hidden switch template name update");
				bsr = &best->spd;
				break;
			}
		}

		if (bsr == NULL) {
			/* nothing to instantiate from other group templates either */
			return false;
		}
	}

	/*
	 * this both replaces the child's connection, and flips any
	 * underlying current-connection
	 *
	 * XXX: but this is responder code, there probably isn't a
	 * current-connection - it would have gone straight to current
	 * state>
	 */
	update_state_connection(&child->sa, best);

	child->sa.st_ts_this = ikev2_end_to_ts(&bsr->this);
	child->sa.st_ts_that = ikev2_end_to_ts(&bsr->that);

	ikev2_print_ts(&child->sa.st_ts_this);
	ikev2_print_ts(&child->sa.st_ts_that);

	return true;
}

/* check TS payloads, response */
bool v2_process_ts_response(struct child_sa *child,
			    struct msg_digest *md)
{
	passert(child->sa.st_sa_role == SA_INITIATOR);
	passert(v2_msg_role(md) == MESSAGE_RESPONSE);

	struct connection *c = child->sa.st_connection;

	struct traffic_selectors tsi = { .nr = 0, };
	struct traffic_selectors tsr = { .nr = 0, };
	if (!v2_parse_tss(md, &tsi, &tsr)) {
		return false;
	}

	/* check TS payloads */
	{
		int bestfit_n, bestfit_p, bestfit_pr;
		int best_tsi_i, best_tsr_i;
		bestfit_n = -1;
		bestfit_p = -1;
		bestfit_pr = -1;

		/* Check TSi/TSr https://tools.ietf.org/html/rfc5996#section-2.9 */
		DBG(DBG_CONTROLMORE,
		    DBG_log("TS: check narrowing - we are responding to I2"));


		DBGF(DBG_MASK, "Checking %u TSi and %u TSr selectors, looking for exact match",
		     tsi.nr, tsr.nr);

		{
			const struct spd_route *sra = &c->spd;
			/* initiator */
			const struct ends e = {
				.i = &sra->this,
				.r = &sra->that,
			};

			int bfit_n = ikev2_evaluate_connection_fit(c, &e, &tsi, &tsr);

			if (bfit_n > bestfit_n) {
				DBG(DBG_CONTROLMORE,
				    DBG_log("prefix fitness found a better match c %s",
					    c->name));

				/* initiator */
				enum narrowing initiator_widening =
					(c->policy & POLICY_IKEV2_ALLOW_NARROWING)
					? END_WIDER_THAN_TS
					: END_EQUALS_TS;
				int bfit_p = ikev2_evaluate_connection_port_fit(initiator_widening,
										&e, &tsi, &tsr,
										&best_tsi_i,
										&best_tsr_i);

				if (bfit_p > bestfit_p) {
					DBG(DBG_CONTROLMORE,
					    DBG_log("port fitness found better match c %s, tsi[%d],tsr[%d]",
						    c->name, best_tsi_i, best_tsr_i));

					int bfit_pr = ikev2_evaluate_connection_protocol_fit(initiator_widening,
											     &e, &tsi, &tsr,
											     &best_tsi_i,
											     &best_tsr_i);

					if (bfit_pr > bestfit_pr) {
						DBG(DBG_CONTROLMORE,
						    DBG_log("protocol fitness found better match c %s, tsi[%d], tsr[%d]",
							    c->name, best_tsi_i,
							    best_tsr_i));
						bestfit_p = bfit_p;
						bestfit_n = bfit_n;
					} else {
						DBG(DBG_CONTROLMORE,
						    DBG_log("protocol fitness rejected c %s",
							    c->name));
					}
				} else {
					DBG(DBG_CONTROLMORE,
							DBG_log("port fitness rejected c %s",
								c->name));
				}
			} else {
				DBG(DBG_CONTROLMORE,
				    DBG_log("prefix fitness rejected c %s",
					    c->name));
			}
		}

		if (bestfit_n > 0 && bestfit_p > 0) {
			DBG(DBG_CONTROLMORE,
			    DBG_log("found an acceptable TSi/TSr Traffic Selector"));
			struct state *st = &child->sa;
			memcpy(&st->st_ts_this, &tsi.ts[best_tsi_i],
			       sizeof(struct traffic_selector));
			memcpy(&st->st_ts_that, &tsr.ts[best_tsr_i],
			       sizeof(struct traffic_selector));
			ikev2_print_ts(&st->st_ts_this);
			ikev2_print_ts(&st->st_ts_that);

			ip_subnet tmp_subnet_i;
			ip_subnet tmp_subnet_r;
			rangetosubnet(&st->st_ts_this.net.start,
				      &st->st_ts_this.net.end, &tmp_subnet_i);
			rangetosubnet(&st->st_ts_that.net.start,
				      &st->st_ts_that.net.end, &tmp_subnet_r);

			c->spd.this.client = tmp_subnet_i;
			c->spd.this.port = st->st_ts_this.startport;
			c->spd.this.protocol = st->st_ts_this.ipprotoid;
			setportof(htons(c->spd.this.port),
				  &c->spd.this.host_addr);
			setportof(htons(c->spd.this.port),
				  &c->spd.this.client.addr);

			c->spd.this.has_client =
				!(subnetishost(&c->spd.this.client) &&
				addrinsubnet(&c->spd.this.host_addr,
					  &c->spd.this.client));

			c->spd.that.client = tmp_subnet_r;
			c->spd.that.port = st->st_ts_that.startport;
			c->spd.that.protocol = st->st_ts_that.ipprotoid;
			setportof(htons(c->spd.that.port),
				  &c->spd.that.host_addr);
			setportof(htons(c->spd.that.port),
				  &c->spd.that.client.addr);

			c->spd.that.has_client =
				!(subnetishost(&c->spd.that.client) &&
				addrinsubnet(&c->spd.that.host_addr,
					  &c->spd.that.client));
		} else {
			DBG(DBG_CONTROLMORE,
			    DBG_log("reject responder TSi/TSr Traffic Selector"));
			/* prevents parent from going to I3 */
			return false;
		}
	} /* end of TS check block */
	return true;
}
