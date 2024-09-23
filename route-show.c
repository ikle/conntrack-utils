/*
 * Routing Tables Viewer
 *
 * Copyright (c) 2016-2024 Alexei A. Smekalkine <ikle@ikle.ru>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdio.h>

#include <sys/param.h>		/* MAX			*/
#include <sys/socket.h>		/* AF_INET*		*/

#include <net/if.h>		/* if_indextoname	*/
#include <arpa/inet.h>

#include <linux/icmpv6.h>	/* ICMPV6_ROUTER_PREF_*	*/
#include <netlink/netlink.h>
#include <netlink/msg.h>

#include "nl-monitor.h"
#include "rt-label.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a)  (sizeof (a) / sizeof ((a)[0]))
#endif

static int is_host_addr (int family, int prefix)
{
	return	(family == AF_INET  && prefix == 32 ) ||
		(family == AF_INET6 && prefix == 128);
}

static void show_proto (unsigned char index)
{
	const char *label = rt_proto (index);

	if (label != NULL)
		printf (" proto %s", label);
	else
		printf (" proto %u", index);
}

static void show_scope (unsigned char index)
{
	const char *label = rt_scope (index);

	if (index == RT_SCOPE_UNIVERSE)
		return;

	if (label != NULL)
		printf (" scope %s", label);
	else
		printf (" scope %u", index);
}

static void show_table (unsigned index)
{
	const char *label = rt_table (index);

	if (index == RT_TABLE_MAIN)
		return;

	if (label != NULL)
		printf (" table %s", label);
	else
		printf (" table %u", index);
}

static const char *get_route_type (unsigned i, char *buf, size_t size)
{
	static const char *map[] = {
		"unspec", "unicast", "local", "broadcast", "anycast",
		"multicast", "blackhole", "unreachable", "prohibit", "throw",
		"nat", "xresolve"
	};

	if (i < ARRAY_SIZE (map))
		return map[i];

	snprintf (buf, size, "type-%u", i);
	return buf;
}

static const char *get_pref (int index)
{
	switch (index) {
	case ICMPV6_ROUTER_PREF_MEDIUM:		return "medium";
	case ICMPV6_ROUTER_PREF_HIGH:		return "high";
	case ICMPV6_ROUTER_PREF_INVALID:	return "invalid";
	case ICMPV6_ROUTER_PREF_LOW:		return "low";
	default:				return "unknown";
	}
}

static void show_route_flags (unsigned flags)
{
	if (flags & RTNH_F_DEAD)	printf (" dead");
	if (flags & RTNH_F_PERVASIVE)	printf (" pervasive");
	if (flags & RTNH_F_ONLINK)	printf (" onlink");
	if (flags & RTNH_F_OFFLOAD)	printf (" offload");
	if (flags & RTNH_F_LINKDOWN)	printf (" linkdown");
	if (flags & RTNH_F_UNRESOLVED)	printf (" unresolved");
	if (flags & RTNH_F_TRAP)	printf (" trap");

	flags &= ~0x7f;

	if (flags != 0)
		printf (" flags %x", flags);
}

struct route_info {
	unsigned char family, dst_len, proto, scope, type;
	unsigned flags;
	int dev, metric, table, mark, pref, expire;
	void *dst, *via, *src;
};

static void route_info_init (struct route_info *o, struct rtmsg *rtm)
{
	memset (o, 0, sizeof (*o));

	o->family  = rtm->rtm_family;
	o->dst_len = rtm->rtm_dst_len;
	o->proto   = rtm->rtm_protocol;
	o->scope   = rtm->rtm_scope;
	o->type    = rtm->rtm_type;
	o->flags   = rtm->rtm_flags;

	o->metric = -1;
	o->table = rtm->rtm_table;
	o->pref = -1;
}

static void route_info_set_rta (struct route_info *o, struct rtattr *rta)
{
	switch (rta->rta_type) {
	case RTA_DST:		o->dst    =           RTA_DATA (rta); break;
	case RTA_GATEWAY:	o->via    =           RTA_DATA (rta); break;
	case RTA_OIF:		o->dev    = *(int *)  RTA_DATA (rta); break;
	case RTA_PREFSRC:	o->src    =           RTA_DATA (rta); break;
	case RTA_PRIORITY:	o->metric = *(int *)  RTA_DATA (rta); break;
	case RTA_TABLE:		o->table  = *(int *)  RTA_DATA (rta); break;
	case RTA_MARK:		o->mark   = *(int *)  RTA_DATA (rta); break;
	case RTA_PREF:		o->pref   = *(char *) RTA_DATA (rta); break;
	case RTA_EXPIRES:	o->expire = *(int *)  RTA_DATA (rta); break;
	}
}

static void route_info_show (struct route_info *o)
{
	char buf[MAX (INET6_ADDRSTRLEN, IF_NAMESIZE)];
	const char *p;

	if (o->type > RTN_UNICAST)
		printf ("%s ", get_route_type (o->type, buf, sizeof (buf)));

	if (o->dst == NULL)
		printf ("default");
	else {
		p = inet_ntop (o->family, o->dst, buf, sizeof (buf));
		printf ("%s", p);

		if (!is_host_addr (o->family, o->dst_len))
			printf ("/%d", o->dst_len);
	}

	if (o->via != NULL) {
		p = inet_ntop (o->family, o->via, buf, sizeof (buf));
		printf (" via %s", p);
	}

	if (o->dev > 0) {
		if ((p = if_indextoname (o->dev, buf)) != NULL)
			printf (" dev %s", p);
		else
			printf (" dev %d", o->dev);
	}

	if (o->table != RT_TABLE_UNSPEC)
		show_table (o->table);

	show_proto (o->proto);
	show_scope (o->scope);

	if (o->src != NULL) {
		p = inet_ntop (o->family, o->src, buf, sizeof (buf));
		printf (" src %s", p);
	}

	if (o->metric >= 0)
		printf (" metric %d", o->metric);

	show_route_flags (o->flags);

	if (o->pref >= 0)
		printf (" pref %s", get_pref (o->pref));

	printf ("\n");
}

static int process_route (struct nlmsghdr *h, void *ctx)
{
	struct rtmsg *rtm = NLMSG_DATA (h);
	struct rtattr *rta;
	int len;
	struct route_info ri;

	if (rtm->rtm_family != AF_INET && rtm->rtm_family != AF_INET6)
		return 0;

	if (rtm->rtm_table == RT_TABLE_LOCAL)
		return 0;

	route_info_init (&ri, rtm);

	for (
		rta = RTM_RTA (rtm), len = RTM_PAYLOAD (h);
		RTA_OK (rta, len);
		rta = RTA_NEXT (rta, len)
	)
		route_info_set_rta (&ri, rta);

	route_info_show (&ri);
	return 0;
}

static int cb (struct nl_msg *m, void *ctx)
{
	struct nlmsghdr *h = nlmsg_hdr (m);

	return h->nlmsg_type == RTM_NEWROUTE ? process_route (h, ctx) : 0;
}

int main (void)
{
	int ret;

	if ((ret = nl_execute (cb, NETLINK_ROUTE, RTM_GETROUTE)) < 0) {
		nl_perror (ret, "netlink show");
		return 1;
	}

	return 0;
}
