/*
 * Linux NetLink Monitor
 *
 * Copyright (c) 2016-2024 Alexei A. Smekalkine <ikle@ikle.ru>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdarg.h>
#include <netlink/route/rtnl.h>

#include "nl-monitor.h"

/*
 * Function takes message callback, netlink type and zero-terminated list
 * of netlink groups
 */
int nl_monitor (nl_recvmsg_msg_cb_t cb, int type, ...)
{
	struct nl_sock *h;
	va_list ap;
	int group;
	int ret;

	if ((h = nl_socket_alloc ()) == NULL)
		return -1;

	/* notifications do not use sequence numbers */
	nl_socket_disable_seq_check (h);

	ret = nl_socket_modify_cb (h, NL_CB_VALID, NL_CB_CUSTOM, cb, NULL);
	if (ret < 0)
		return ret;

	if ((ret = nl_connect (h, type)) < 0)
		return ret;

	va_start (ap, type);

	while ((group = va_arg (ap, int)) != 0)
		nl_socket_add_membership (h, group);

	va_end (ap);

	while ((ret = nl_recvmsgs_default (h)) >= 0) {}

	nl_close (h);
	nl_socket_free (h);
	return ret;
}
