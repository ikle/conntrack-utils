/*
 * The simple carrier detector to renew DHCP for udhcpc
 *
 * Copyright (c) 2016-2020 Alexei A. Smekalkine <ikle@ikle.ru>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <signal.h>
#include <stdio.h>
#include <sys/types.h>

#include <net/if.h>
#include <syslog.h>
#include <unistd.h>

#include <netlink/netlink.h>
#include <netlink/msg.h>

#include "nl-monitor.h"

static long udhcpc_get_pid (const char *link)
{
	char name[IFNAMSIZ + 1], *p, path[128];
	int ret;
	FILE *f;
	long pid;

	snprintf (name, sizeof (name), "%s", link);

	for (p = name; *p != '\0'; ++p)
		if (*p == '.')
			*p = '_';

	ret = snprintf (path, sizeof (path), "/var/run/udhcpc.%s.pid", name);

	if (ret >= sizeof (path))  /* buffer too small, should never happens */
		return 0;

	if ((f = fopen (path, "r")) == NULL)
		return 0;

	ret = fscanf (f, "%ld", &pid) == 1;
	fclose (f);
	return ret ? pid : 0;
}

static int udhcpc_renew (const char *link)
{
	long pid;

	if ((pid = udhcpc_get_pid (link)) <= 0)
		return 0;

	return kill (pid, SIGUSR1) == 0;
}

#define CARRIER_MASK	(IFF_UP | IFF_RUNNING)
#define CARRIER_ON	(IFF_UP | IFF_RUNNING)
#define CARRIER_OFF	(IFF_UP)

static int action (const char *name, unsigned flags)
{
	if (name == NULL || (flags & CARRIER_MASK) != CARRIER_ON)
		return 0;

	return udhcpc_renew (name);
}

static int process_link (struct nlmsghdr *h, struct ifinfomsg *o, void *ctx)
{
	struct rtattr *rta;
	int len;
	const char *name = NULL;

	for (
		rta = IFLA_RTA (o), len = IFLA_PAYLOAD (h);
		RTA_OK (rta, len);
		rta = RTA_NEXT (rta, len)
	)
		switch (rta->rta_type) {
		case IFLA_IFNAME:
			name = RTA_DATA (rta);
			break;
		}

	if (action (name, o->ifi_flags))
		syslog (LOG_NOTICE,
			"%s: carrier detected, requested DHCP renew", name);

	return 0;
}

static int cb (struct nl_msg *m, void *ctx)
{
	struct nlmsghdr *h = nlmsg_hdr (m);

	switch (h->nlmsg_type) {
	case RTM_NEWLINK:
		return process_link (h, nlmsg_data (h), ctx);
	}

	return 0;
}

int main (void)
{
	int ret;

	if (daemon (0, 0) != 0) {
		perror ("udhcpc-monitor: cannot daemonize");
		return 1;
	}

	openlog ("udhcpc-monitor", 0, LOG_DAEMON);

	if ((ret = nl_execute (cb, NETLINK_ROUTE, RTM_GETLINK)) < 0 ||
	    (ret = nl_monitor (cb, NETLINK_ROUTE, RTNLGRP_LINK, 0)) < 0) {
		syslog (LOG_ERR, "netlink error: %s", nl_geterror (ret));
		return 1;
	}

	return 0;
}
