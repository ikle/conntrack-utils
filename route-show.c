/*
 * Routing Tables Viewer
 *
 * Copyright (c) 2016-2024 Alexei A. Smekalkine <ikle@ikle.ru>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdio.h>

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

static int show_route_type (struct route_info *o, int cont, int json)
{
	static const char *map[] = {
		"unspec", "unicast", "local", "broadcast", "anycast",
		"multicast", "blackhole", "unreachable", "prohibit", "throw",
		"nat", "xresolve"
	};

	if (o->type < RTN_LOCAL)
		return cont;

	if (o->type < ARRAY_SIZE (map))
		printf ("%s ", map[o->type]);
	else
		printf ("type-%u ", o->type);

	return 1;
}

static int show_route_dst (struct route_info *o, int cont, int json)
{
	char buf[INET6_ADDRSTRLEN];
	const char *p;

	if (o->dst == NULL)
		printf ("default");
	else {
		p = inet_ntop (o->family, o->dst, buf, sizeof (buf));
		printf ("%s", p);

		if (!is_host_addr (o->family, o->dst_len))
			printf ("/%d", o->dst_len);
	}

	return 1;
}

static int show_route_via (struct route_info *o, int cont, int json)
{
	char buf[INET6_ADDRSTRLEN];
	const char *p;

	if (o->via == NULL)
		return cont;

	p = inet_ntop (o->family, o->via, buf, sizeof (buf));
	printf (" via %s", p);
	return 1;
}

static int show_route_dev (struct route_info *o, int cont, int json)
{
	char buf[IF_NAMESIZE];
	const char *p;

	if (o->dev <= 0)
		return cont;

	if ((p = if_indextoname (o->dev, buf)) != NULL)
		printf (" dev %s", p);
	else
		printf (" dev %d", o->dev);

	return 1;
}

static int show_route_table (struct route_info *o, int cont, int json)
{
	const char *label = rt_table (o->table);

	if (o->table == RT_TABLE_UNSPEC || o->table == RT_TABLE_MAIN)
		return cont;

	if (label != NULL)
		printf (" table %s", label);
	else
		printf (" table %u", o->table);

	return 1;
}

static int show_route_proto (struct route_info *o, int cont, int json)
{
	const char *label = rt_proto (o->proto);

	if (label != NULL)
		printf (" proto %s", label);
	else
		printf (" proto %u", o->scope);

	return 1;
}

static int show_route_scope (struct route_info *o, int cont, int json)
{
	const char *label = rt_scope (o->scope);

	if (o->scope == RT_SCOPE_UNIVERSE)
		return cont;

	if (label != NULL)
		printf (" scope %s", label);
	else
		printf (" scope %u", o->scope);

	return 1;
}

static int show_route_src (struct route_info *o, int cont, int json)
{
	char buf[INET6_ADDRSTRLEN];
	const char *p;

	if (o->src == NULL)
		return cont;

	p = inet_ntop (o->family, o->src, buf, sizeof (buf));
	printf (" src %s", p);
	return 1;
}

static int show_route_metric (struct route_info *o, int cont, int json)
{
	if (o->metric < 0)
		return cont;

	printf (" metric %d", o->metric);
	return 1;
}

static int show_route_flags (struct route_info *o, int cont, int json)
{
	if (o->flags & RTNH_F_DEAD)		printf (" dead");
	if (o->flags & RTNH_F_PERVASIVE)	printf (" pervasive");
	if (o->flags & RTNH_F_ONLINK)		printf (" onlink");
	if (o->flags & RTNH_F_OFFLOAD)		printf (" offload");
	if (o->flags & RTNH_F_LINKDOWN)		printf (" linkdown");
	if (o->flags & RTNH_F_UNRESOLVED)	printf (" unresolved");
	if (o->flags & RTNH_F_TRAP)		printf (" trap");

	if ((o->flags & ~0x7f) != 0)
		printf (" flags %x", o->flags & ~0x7f);

	return o->flags != 0 ? 1 : cont;
}

static int show_route_pref (struct route_info *o, int cont, int json)
{
	static const char *map[] = { "medium", "high", "invalid", "low" };

	if (o->pref < 0)
		return cont;

	if (o->pref < ARRAY_SIZE (map))
		printf (" pref %s", map[o->pref]);
	else
		printf (" pref %u ", o->pref);

	return 1;
}

static void route_info_show (struct route_info *o, int json)
{
	int c = 0;

	c = show_route_type   (o, c, json);
	c = show_route_dst    (o, c, json);
	c = show_route_via    (o, c, json);
	c = show_route_dev    (o, c, json);
	c = show_route_table  (o, c, json);
	c = show_route_proto  (o, c, json);
	c = show_route_scope  (o, c, json);
	c = show_route_src    (o, c, json);
	c = show_route_metric (o, c, json);
	c = show_route_flags  (o, c, json);
	c = show_route_pref   (o, c, json);

	if (c)
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

	route_info_show (&ri, 0);
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
