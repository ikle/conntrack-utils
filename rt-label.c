/*
 * Routing Tables Labels
 *
 * Copyright (c) 2018-2024 Alexei A. Smekalkine <ikle@ikle.ru>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdio.h>
#include <string.h>

#include "rt-label.h"

#define ARRAY_SIZE(a)  (sizeof (a) / sizeof ((a)[0]))

#define ROOT  "/etc/iproute2"

static void read_table (const char *path, char *table[], size_t size)
{
	FILE *f;
	char line[512], *p;
	int index;

	if ((f = fopen (path, "r")) == NULL)
		return;

	while ((p = fgets (line, sizeof (line), f)) != NULL)
		if (sscanf (p, " %i %s", &index, line) == 2 &&
		    index >= 0 && index < size)
			table[index] = strdup (line);
	fclose (f);
}

const char *rt_proto (unsigned char index)
{
	static char *table[256];
	static int loaded;

	if (!loaded) {
		read_table (ROOT "/rt_protos", table, ARRAY_SIZE (table));
		loaded = 1;
	}

	return index < ARRAY_SIZE (table) ? table[index] : NULL;
}

const char *rt_scope (unsigned char index)
{
	static char *table[256];
	static int loaded;

	if (!loaded) {
		read_table (ROOT "/rt_scopes", table, ARRAY_SIZE (table));
		loaded = 1;
	}

	return index < ARRAY_SIZE (table) ? table[index] : NULL;
}

const char *rt_table (unsigned index)
{
	static char *table[256];
	static int loaded;

	if (!loaded) {
		read_table (ROOT "/rt_tables", table, ARRAY_SIZE (table));
		loaded = 1;
	}

	return index < ARRAY_SIZE (table) ? table[index] : NULL;
}
