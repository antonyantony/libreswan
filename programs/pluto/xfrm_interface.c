/*
 * xfrmi interface related functions
 *
 * Copyright (C) 2018-2019 Antony Antony <antony@phenome.org>
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

#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdint.h>
#include <net/if.h>

#include <linux/rtnetlink.h>
#include <linux/rtnetlink.h>
#include <linux/if_addr.h>
#include <linux/if_link.h>

#include "netlink_attrib.h"
#include "xfrm_interface.h"
#include "lswlog.h"
#include "connections.h"
#include "server.h" /* for struct iface_port */

#if !defined(USE_XFRM_INTERFACE) || !defined(NETKEY_SUPPORT)
# error this file should only compile when NETKEY_SUPPORT & USE_XFRM_INTERFACE are defined
#endif

#define IPSEC0_XFRM_IF_ID (1U)

struct nl_ifi_req {
	struct nlmsghdr n;
	struct ifinfomsg i;
	char data[MAX_NETLINK_DATA_SIZE];
};

static int xfrm_interface_support;
static bool stale_checked;
static uint32_t xfrm_interface_id = IPSEC0_XFRM_IF_ID; /* XFRMA_IF_ID && XFRMA_SET_MARK */

static bool nl_query_rsp(struct nlmsghdr *hdr, int protocol, struct
		nlm_resp *rsp)
{
	size_t len;
	ssize_t r;
	struct sockaddr_nl addr;
	int nl_fd = socket(AF_NETLINK, SOCK_DGRAM, protocol);

	if (nl_fd < 0) {
		LOG_ERRNO(errno, "socket() in nl_query_rsp()");
		return TRUE;
	}

	if (fcntl(nl_fd, F_SETFL, O_NONBLOCK) != 0) {
		LOG_ERRNO(errno, "fcntl(O_NONBLOCK) in nl_query_rsp()");
		close(nl_fd);
		return TRUE;
	}

	/* hdr->nlmsg_seq = ++seq; */
	len = hdr->nlmsg_len;
	do {
		r = write(nl_fd, hdr, len);
	} while (r < 0 && errno == EINTR);
	if (r < 0) {
		LOG_ERRNO(errno, "netlink write() xfrm_migrate_support lookup");
		close(nl_fd);
		return FALSE;
	} else if ((size_t)r != len) {
		loglog(RC_LOG_SERIOUS,
			"ERROR: netlink write() xfrm_migrate_support message truncated: %zd instead of %zu",
			r, len);
		close(nl_fd);
		return TRUE;
	}

	for (;;) {
		socklen_t alen = sizeof(addr);

		r = recvfrom(nl_fd, &rsp, sizeof(rsp), 0,
				(struct sockaddr *)&addr, &alen);
		if (r < 0) {
			if (errno == EINTR) {
				continue;
			} else if (errno == EAGAIN) {
				/* old kernel F22 - dos not return proper error ??? */
				DBG(DBG_KERNEL, DBG_log("ignore EAGAIN in %s assume MOBIKE migration is supported", __func__));
				break;
			}
		}
		break;
	}

	close(nl_fd);

	return FALSE;
}


static void  init_nl_ifi(struct nl_ifi_req *req, uint16_t type, uint16_t flags)
{
	req->n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req->n.nlmsg_flags = flags;
	req->n.nlmsg_type = type;
	req->i.ifi_family = AF_UNSPEC;
}

static bool link_set_up_nl_msg(const char *if_name, struct nl_ifi_req *req)
{
	init_nl_ifi(req, RTM_NEWLINK, NLM_F_REQUEST);
	req->i.ifi_change |= IFF_UP;
	req->i.ifi_flags |= IFF_UP;
	req->i.ifi_index = if_nametoindex(if_name);
	if (req->i.ifi_index == 0) {
		LOG_ERRNO(errno, "link_del_nl_msg() can not find index of %s",
				if_name);
		return TRUE;
	}
	return FALSE;
}

static bool link_del_nl_msg(const char *if_name, struct nl_ifi_req *req)
{
	init_nl_ifi(req, RTM_DELLINK, NLM_F_REQUEST);
	req->i.ifi_index = if_nametoindex(if_name);
	if (req->i.ifi_index == 0) {
		LOG_ERRNO(errno, "link_del_nl_msg() can not find index of %s",
				if_name);
		return TRUE;
	}
	return FALSE;
}

static bool link_add_nl_msg(const char *if_name,
                const char *dev_name, const uint32_t if_id,
		struct nl_ifi_req *req)
{

	char link_type[] = "xfrm";
	init_nl_ifi(req, RTM_NEWLINK, NLM_F_REQUEST | NLM_F_CREATE| NLM_F_EXCL);

	nl_addattrstrz(&req->n, sizeof(struct nl_ifi_req), IFLA_IFNAME, if_name);

	struct rtattr *linkinfo;
	linkinfo = nl_addattr_nest(&req->n, sizeof(struct nl_ifi_req), IFLA_LINKINFO);
	nl_addattr_l(&req->n, sizeof(struct nl_ifi_req), IFLA_INFO_KIND, link_type,
			strlen(link_type));

	struct rtattr *xfrm_link = nl_addattr_nest(&req->n, sizeof(struct nl_ifi_req), IFLA_INFO_DATA);
	nl_addattr32(&req->n, 1024, IFLA_XFRM_IF_ID, if_id);

	if (dev_name != NULL) {
		uint32_t dev_link_id; /* e.g link id of the interace, eth0 */
		dev_link_id = if_nametoindex(dev_name);
		if (dev_link_id != 0) {
			nl_addattr32(&req->n, 1024, IFLA_XFRM_LINK, dev_link_id);
		} else {
			LOG_ERRNO(errno, "Can not find interface index for device %s",
					dev_name);
			return TRUE;
		}
	}

	nl_addattr_nest_end(&req->n, xfrm_link);

	nl_addattr_nest_end(&req->n, linkinfo);

	return FALSE;
}


bool ip_link_set_up(const char *if_name)
{
	struct nl_ifi_req req;
	zero(&req);
	if(link_set_up_nl_msg(if_name, &req))
	{
		libreswan_log_rc(RC_FATAL, "ERROR: ip_link_set_up() creating netlink message failed");
		return TRUE;
	}

	struct nlm_resp nl_rsp;
	if (nl_query_rsp(&req.n, NETLINK_ROUTE, &nl_rsp))
	{
		libreswan_log_rc(RC_FATAL, "ERROR:ip_link_set_up() netlink query dev %s", if_name);

	} else {
		/* netlink query succeeded. check NL response */
		if (nl_rsp.n.nlmsg_type == NLMSG_ERROR) {
			libreswan_log_rc(RC_INFORMATIONAL, "deleting interface %s failed", if_name);

			return TRUE;
		}
	}
	return FALSE;
}

static bool ip_link_del(const char *if_name)
{
	struct nl_ifi_req req;
	zero(&req);
	if(link_del_nl_msg(if_name, &req))
	{
		libreswan_log_rc(RC_FATAL, "ERROR: link_del_nl_msg() creating netlink message failed");
		return TRUE;
	}

	struct nlm_resp nl_rsp;
	if (nl_query_rsp(&req.n, NETLINK_ROUTE, &nl_rsp))
	{
		libreswan_log_rc(RC_FATAL, "ERROR: nl_query_rsp() netlink query failed");

	} else {
		/* netlink query succeeded. check NL response */
		if (nl_rsp.n.nlmsg_type == NLMSG_ERROR) {
			libreswan_log_rc(RC_INFORMATIONAL, "deleting interface %s failed", if_name);

			return TRUE;
		}
	}
	return FALSE;
}

static bool ip_link_add_xfrmi(const char *if_name, const char *dev_name, const uint32_t if_id)
{
	struct nl_ifi_req req;
	zero(&req);
	if(link_add_nl_msg(if_name, dev_name, if_id, &req))
	{
		libreswan_log_rc(RC_FATAL, "ERROR: nl_query_rsp() creating netlink message failed");
		return TRUE;
	}

	struct nlm_resp nl_rsp;
	if (nl_query_rsp(&req.n, NETLINK_ROUTE, &nl_rsp))
	{
		libreswan_log_rc(RC_FATAL, "ERROR: nl_query_rsp() netlink query failed");

	} else {
		/* netlink query succeeded. check NL response */
		if (nl_rsp.n.nlmsg_type == NLMSG_ERROR &&
				nl_rsp.u.e.error == -ENOPROTOOPT) {
			libreswan_log_rc(RC_FATAL,"CONFIG_XFRM_INTERFACE fail got ENOPROTOOPT");

			return TRUE;
		}
	}

	return FALSE;
}

static bool dev_exist_check(const char *dev_name, char *dev_type)
{
	unsigned int if_id = if_nametoindex(dev_name); /* basee line check */
	if (if_id == 0) {
		LOG_ERRNO(errno, "FATAL can not find device %s type %s",
				dev_name, dev_type );
		return TRUE;
	}
	return FALSE;
}

/*
 * one fine day DHR will swing his uncomplication wand here
 * AA_201902 can this be err_t ??? then err = ipsec0_support_test(if_name, lo);
 * error: assignment discards ‘const’ qualifier from pointer target type [-Werror=discarded-qualifiers]
 */
static char *ipsec0_support_test(const char *if_name, const char *dev_name)
{
	char *err = NULL; /* success */
	if (ip_link_add_xfrmi(if_name, dev_name, xfrm_interface_id)) {
		/*
		 *
		 * would this this be temporary error?
		 * missing interface or so? e.g wlan/ppp which would
		 * appear later on? In that case pluto check again.
		 */
		xfrm_interface_support = -1;
	} else {
		if (dev_exist_check(if_name, "xfrmi")) {
			/*
			 * assume kernel support is not enabled.
			 * ip link add ipsec0 type xfrm xfrmi-id 6 dev eth0
			 * can be quiet when kernel has no CONFIG_XFRM_INTERFACE=no 
			 */
			xfrm_interface_support = -1;
			err = "missing CONFIG_XFRM_INTERFACE support in kernel";
		} else {
			dbg("succeded creating test xfrmi device %s@%s",
					if_name, dev_name);
			ip_link_del(if_name); /* ignore return value??? */
			xfrm_interface_support = 1; /* success */
		}
	}

	return err;
}

err_t xfrm_iface_supported(void)
{
	char *err = NULL; /* success */

	if (xfrm_interface_support == 0) {
		char if_name[IFNAMSIZ];
		char lo[]  ="lo";

		if (dev_exist_check(lo, "real")) {
			/* possibly no need to pancic may be get smarter one day */
			xfrm_interface_support = -1;
			return ("Could not create find real device needed to test xfrmi support");
		}

		snprintf(if_name, sizeof(if_name), XFRMI_DEV_FORMAT,
				(--xfrm_interface_id)); /* first one ipsec0 */

		unsigned int if_id = if_nametoindex(if_name);
		int e = errno;
		if (if_id == 0 && (e == ENXIO || e == ENODEV)) {
			err = ipsec0_support_test(if_name, lo);
		} else if (if_id == 0) {
			LOG_ERRNO(e, "FATAL unexpeted error in xfrm_iface_supported() while checking device %s",
					if_name);
			xfrm_interface_support = -1;
			err = "can not decide xfrmi support. assumed no.";
		} else {
			/*
			 * may be more extensive checks?
			 * such if it is a xfrmi device or something else
			 */
			loglog(RC_LOG_SERIOUS, "conflict %s already exist can not support xfrm-interface. May be leftover from previous pluto?",
					if_name);
			xfrm_interface_support = -1;
			err = "device name conflict in xfrm_iface_supported()";
		}
	}

	if (xfrm_interface_support < 0 && err == NULL)
		err = "may be missing CONFIG_XFRM_INTERFACE support in kernel";

	return err;
}

bool setup_xfrm_interface(struct connection *c)
{

	if (c->xfrm_if_id == 0) {
		if (c->xfrm_if == yna_yes) {
			c->xfrm_if_id = IPSEC0_XFRM_IF_ID;
		} else if (c->xfrm_if == yna_auto)  {
			c->xfrm_if_id = ++xfrm_interface_id;
			passert(xfrm_interface_id < UINT32_MAX);
		} /* else {  could be passert() } */

		passert(c->xfrm_if_id > 0);

		char if_name[IFNAMSIZ * 2];
		snprintf(if_name, sizeof(if_name), XFRMI_DEV_FORMAT, (c->xfrm_if_id-1)); 
		passert(strlen(if_name) < IFNAMSIZ);
		c->xfrm_if_name = clone_str(if_name, "xfrm_if_name in c");
	}

	if (ip_link_add_xfrmi(c->xfrm_if_name, c->interface->ip_dev->id_rname,
				c->xfrm_if_id))
		return TRUE;

	if (ip_link_set_up(c->xfrm_if_name))
			return TRUE;

	return FALSE;
}

/* at start call this to see if there are any stale interface lying around. */
bool stale_xfrmi_interfaces(void)
{
	if (stale_checked)
		return FALSE; /* possibly from second whack listen */

	stale_checked = TRUE; /* do not re-enter */

	/*
	 * first check quick one do ipsec0 exist. later on add extensive checks
	 * "ip link show type xfrmi" would be better.
	 *  note when type foo is not supported would return success, 0
	 */

	char if_name[IFNAMSIZ];
	snprintf(if_name, sizeof(if_name), XFRMI_DEV_FORMAT, 0); /* first one ipsec0 */

	unsigned int if_id = if_nametoindex(if_name);
	if (if_id != 0) {
		loglog(RC_LOG_SERIOUS, "found an unexpected interface %s if_id=%u From previous pluto run?",
				if_name, if_id);
		return TRUE; /* ERROR */
	} else {
		if (errno == ENXIO || errno == ENODEV) {
			dbg("no stale xfrmi interface '%s' found", if_name);
		} else {
			LOG_ERRNO(errno, "failed stale_xfrmi_interfaces() call if_nametoindex('%s')",if_name);
			return TRUE;
		}
	}
	return FALSE;
}

void free_xfrmi_ipsec0(void)
{
	char if_name[IFNAMSIZ];
	snprintf(if_name, sizeof(if_name), XFRMI_DEV_FORMAT, 0); /* gloabl ipsec0 */
	unsigned int if_id = if_nametoindex(if_name);

	if (if_id > 0) {
		ip_link_del(if_name); /* ignore return value??? */
	}
}
