/*
 * Linux NetLink Requester
 *
 * Copyright (c) 2016-2024 Alexei A. Smekalkine <ikle@ikle.ru>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <netlink/route/rtnl.h>

#include "nl-monitor.h"

/*
 * Function takes message callback, network family, netlink type and
 * command to execute
 */
int nl_execute_ex (nl_recvmsg_msg_cb_t cb, int family, int type, int cmd)
{
	struct nl_sock *h;
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

	ret = nl_rtgen_request (h, cmd, family, NLM_F_REQUEST | NLM_F_ROOT);
	if (ret >= 0)
		while ((ret = nl_recvmsgs_default (h)) > 0) {}

	nl_close (h);
	nl_socket_free (h);
	return ret;
}

int nl_execute (nl_recvmsg_msg_cb_t cb, int type, int cmd)
{
	return nl_execute_ex (cb, AF_UNSPEC, type, cmd);
}
