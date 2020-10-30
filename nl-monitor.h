/*
 * Linux NetLink Monitor Library
 *
 * Copyright (c) 2016-2020 Alexei A. Smekalkine <ikle@ikle.ru>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef _NL_MONITOR_H
#define _NL_MONITOR_H  1

#include <netlink/netlink.h>

/*
 * Function takes message callback, netlink type and zero-terminated list
 * of netlink groups
 */
int nl_monitor (nl_recvmsg_msg_cb_t cb, int type, ...);
int nl_execute (nl_recvmsg_msg_cb_t cb, int type, int cmd);

#endif  /* _NL_MONITOR_H */
