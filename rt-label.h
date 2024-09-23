/*
 * Routing Tables Labels
 *
 * Copyright (c) 2018-2024 Alexei A. Smekalkine <ikle@ikle.ru>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef RT_LABEL_H
#define RT_LABEL_H

const char *rt_proto (unsigned char index);
const char *rt_scope (unsigned char index);
const char *rt_table (unsigned index);

#endif  /* RT_LABEL_H */
