/*
 * Routing Tables Viewer
 *
 * Copyright (c) 2016-2024 Alexei A. Smekalkine <ikle@ikle.ru>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdio.h>
#include <string.h>

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

static int show_str_opt (const char *n, const char *v, int cont, int json)
{
	if (cont)	putchar (json ? ',' : ' ');
	if (json)	printf ("\"%s\":\"%s\"", n, v);
	else		printf ("%s %s", n, v);

	return 1;
}

static int show_int_opt (const char *n, int v, int cont, int json)
{
	if (cont)	putchar (json ? ',' : ' ');
	if (json)	printf ("\"%s\":%d", n, v);
	else		printf ("%s %d", n, v);

	return 1;
}

static int show_str (const char *v, int cont, int json)
{
	if (cont)	putchar (json ? ',' : ' ');
	if (json)	printf ("\"%s\"", v);
	else		printf ("%s", v);

	return 1;
}

#define RTNH_NEXT_NG(rtnh, len)  ((len) -= RTNH_ALIGN((rtnh)->rtnh_len), RTNH_NEXT(rtnh))
#define RTNH_PAYLOAD(rtnh)       ((int)((rtnh)->rtnh_len) - RTNH_LENGTH(0))

struct nexthop_info {
	unsigned char family, flags, hops, dev;
	void *via;
};

static void
nexthop_info_init (struct nexthop_info *o, int family, struct rtnexthop *nh)
{
	struct rtattr *rta;
	int len;

	o->family = family;
	o->flags  = nh->rtnh_flags;
	o->hops   = nh->rtnh_hops;
	o->dev    = nh->rtnh_ifindex;
	o->via    = NULL;

	for (
		rta = RTNH_DATA (nh), len = RTNH_PAYLOAD (nh);
		RTA_OK (rta, len);
		rta = RTA_NEXT (rta, len)
	)
		switch (rta->rta_type) {
		case RTA_GATEWAY:  o->via = RTA_DATA (rta); break;
		}
}

static int show_nexthop_via (struct nexthop_info *o, int cont, int json)
{
	char buf[INET6_ADDRSTRLEN];
	const char *p;

	if (o->via == NULL)
		return cont;

	p = inet_ntop (o->family, o->via, buf, sizeof (buf));
	return show_str_opt (json ? "gateway" : "via", p, cont, json);
}

static int show_nexthop_dev (struct nexthop_info *o, int cont, int json)
{
	char buf[IF_NAMESIZE];
	const char *p;

	if (o->dev <= 0)
		return cont;

	if ((p = if_indextoname (o->dev, buf)) != NULL)
		return show_str_opt ("dev", p, cont, json);

	return show_int_opt ("dev", o->dev, cont, json);
}

static int show_nexthop_weight (struct nexthop_info *o, int cont, int json)
{
	return show_int_opt ("weight", o->hops + 1, cont, json);
}

static int show_hexthop_flags (struct nexthop_info *o, int cont, int json)
{
	static const char *map[] = { "dead", "pervasive", "onlink", "offload",
				     "linkdown", "unresolved", "trap" };
	int i, c = (!json) & cont;

	if (json & cont)  putchar (',');
	if (json)         printf ("\"flags\":[");

	for (i = 0; i < ARRAY_SIZE (map); ++i)
		if (o->flags & (1 << i))
			c = show_str (map[i], c, json);

	if ((o->flags & ~0x7f) != 0 && !json)
		printf (" flags %x", o->flags & ~0x7f), c = 1;

	if (json)  printf ("]");

	return cont | json | c;
}

static int nexthop_info_show (struct nexthop_info *o, int cont, int json)
{
	int c = !json;

	if (json && cont)  putchar (',');
	if (json)          putchar ('{');
	else               printf ("\n\tnexthop");

	c = show_nexthop_via    (o, c, json);
	c = show_nexthop_dev    (o, c, json);
	c = show_nexthop_weight (o, c, json);
	c = show_hexthop_flags  (o, c, json);

	if (json)  putchar ('}');

	return 1;
}

struct route_info {
	unsigned char family, dst_len, proto, scope, type;
	unsigned flags;
	int dev, metric, hops_len, table, mark, pref, expire;
	void *dst, *via, *src, *hops;
};

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

	case RTA_MULTIPATH:
		o->hops     = RTA_DATA    (rta);
		o->hops_len = RTA_PAYLOAD (rta);
		break;
	}
}

static
void route_info_init (struct route_info *o, struct rtmsg *rtm, size_t len)
{
	struct rtattr *rta;

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
	o->hops = NULL;

	for (rta = RTM_RTA (rtm); RTA_OK (rta, len); rta = RTA_NEXT (rta, len))
		route_info_set_rta (o, rta);
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

	if (json)  printf ("\"type\":\"");

	if (o->type < ARRAY_SIZE (map))
		printf ("%s", map[o->type]);
	else
		printf ("type-%u", o->type);

	if (json)  printf ("\"");

	return 1;
}

static int show_route_dst (struct route_info *o, int cont, int json)
{
	char buf[INET6_ADDRSTRLEN];
	const char *p;

	if (cont)  putchar (json ? ',' : ' ');
	if (json)  printf ("\"dst\":\"");

	if (o->dst == NULL)
		printf ("default");
	else {
		p = inet_ntop (o->family, o->dst, buf, sizeof (buf));
		printf ("%s", p);

		if (!is_host_addr (o->family, o->dst_len))
			printf ("/%d", o->dst_len);
	}

	if (json)  printf ("\"");

	return 1;
}

static int show_route_via (struct route_info *o, int cont, int json)
{
	char buf[INET6_ADDRSTRLEN];
	const char *p;

	if (o->via == NULL)
		return cont;

	p = inet_ntop (o->family, o->via, buf, sizeof (buf));
	return show_str_opt (json ? "gateway" : "via", p, cont, json);
}

static int show_route_dev (struct route_info *o, int cont, int json)
{
	char buf[IF_NAMESIZE];
	const char *p;

	if (o->dev <= 0)
		return cont;

	if ((p = if_indextoname (o->dev, buf)) != NULL)
		return show_str_opt ("dev", p, cont, json);

	return show_int_opt ("dev", o->dev, cont, json);
}

static int show_route_table (struct route_info *o, int cont, int json)
{
	const char *label = rt_table (o->table);

	if (o->table == RT_TABLE_UNSPEC || o->table == RT_TABLE_MAIN)
		return cont;

	if (label != NULL)
		return show_str_opt ("table", label, cont, json);

	return show_int_opt ("table", o->table, cont, json);
}

static int show_route_proto (struct route_info *o, int cont, int json)
{
	const char *label = rt_proto (o->proto);
	const char *name  = json ? "protocol" : "proto";

	if (o->proto == RTPROT_BOOT)
		return cont;

	if (label != NULL)
		return show_str_opt (name, label, cont, json);

	return show_int_opt (name, o->proto, cont, json);
}

static int show_route_scope (struct route_info *o, int cont, int json)
{
	const char *label = rt_scope (o->scope);

	if (o->scope == RT_SCOPE_UNIVERSE)
		return cont;

	if (label != NULL)
		return show_str_opt ("scope", label, cont, json);

	return show_int_opt ("scope", o->scope, cont, json);
}

static int show_route_src (struct route_info *o, int cont, int json)
{
	char buf[INET6_ADDRSTRLEN];
	const char *p;

	if (o->src == NULL)
		return cont;

	p = inet_ntop (o->family, o->src, buf, sizeof (buf));
	return show_str_opt (json ? "prefsrc" : "src", p, cont, json);
}

static int show_route_metric (struct route_info *o, int cont, int json)
{
	if (o->metric < 0)
		return cont;

	return show_int_opt ("metric", o->metric, cont, json);
}

static int show_route_flags (struct route_info *o, int cont, int json)
{
	static const char *map[] = { "dead", "pervasive", "onlink", "offload",
				     "linkdown", "unresolved", "trap" };
	int i, c = (!json) & cont;

	if (json & cont)  putchar (',');
	if (json)         printf ("\"flags\":[");

	for (i = 0; i < ARRAY_SIZE (map); ++i)
		if (o->flags & (1 << i))
			c = show_str (map[i], c, json);

	if ((o->flags & ~0x7f) != 0 && !json)
		printf (" flags %x", o->flags & ~0x7f), c = 1;

	if (json)  printf ("]");

	return cont | json | c;
}

static int show_route_pref (struct route_info *o, int cont, int json)
{
	static const char *map[] = { "medium", "high", "invalid", "low" };

	if (o->pref < 0)
		return cont;

	if (o->pref < ARRAY_SIZE (map))
		return show_str_opt ("pref", map[o->pref], cont, json);

	return show_int_opt ("pref", o->pref, cont, json);
}

static int show_route_hops (struct route_info *o, int cont, int json)
{
	struct nexthop_info hop;
	struct rtnexthop *nh;
	int c = 0, len;

	if (o->hops == NULL)
		return cont;

	if (json & cont)  putchar (',');
	if (json)         printf ("\"nexthops\":[");

	for (
		nh = o->hops, len = o->hops_len;
		RTNH_OK (nh, len);
		nh = RTNH_NEXT_NG (nh, len)
	) {
		nexthop_info_init (&hop, o->family, nh);
		c = nexthop_info_show (&hop, c, json);
	}

	if (json)  printf ("]");

	return 1;
}

static int route_info_show (struct route_info *o, int cont, int json)
{
	int c = 0;

	if (json && cont)  putchar (',');
	if (json)          putchar ('{');

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
	c = show_route_hops   (o, c, json);

	putchar (json ? '}' : '\n');
	return 1;
}

static int json;
static int table = RT_TABLE_MAIN;

static int process_route (struct nlmsghdr *h, void *ctx)
{
	static int cont;
	struct rtmsg *rtm = NLMSG_DATA (h);
	struct route_info ri;

	if (rtm->rtm_family != AF_INET && rtm->rtm_family != AF_INET6)
		return 0;

	if (table > 0 && rtm->rtm_table != table)
		return 0;

	route_info_init (&ri, rtm, RTM_PAYLOAD (h));

	cont = route_info_show (&ri, cont, json);
	return 0;
}

static int cb (struct nl_msg *m, void *ctx)
{
	struct nlmsghdr *h = nlmsg_hdr (m);

	return h->nlmsg_type == RTM_NEWROUTE ? process_route (h, ctx) : 0;
}

#define GET_OPT(opt, action)						\
	do {								\
		if (argc > 1 && strcmp (opt, argv[1]) == 0)		\
			action, --argc, ++argv;				\
	}								\
	while (0)

int main (int argc, char *argv[])
{
	int family = AF_UNSPEC, ret;

	GET_OPT ("-j", json = 1);
	GET_OPT ("-4", family = AF_INET);
	GET_OPT ("-6", family = AF_INET6);
	GET_OPT ("-a", table  = 0);

	if (json)  putchar ('[');

	if ((ret = nl_execute_ex (cb, family, NETLINK_ROUTE, RTM_GETROUTE)) < 0) {
		nl_perror (ret, "netlink show");
		return 1;
	}

	if (json)  printf ("]\n");

	return 0;
}
