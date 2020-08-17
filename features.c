// SPDX-License-Identifier: GPL-2.0
/*
 * Author: Coly Li <colyli@suse.de>
 *
 * Inspired by e2fsprogs features compat/incompat/ro_compat
 * related code.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <string.h>

#include "bcache.h"

struct feature {
	int		compat;
	unsigned int	mask;
	const char	*string;
};

static struct feature feature_list[] = {
	{BCH_FEATURE_INCOMPAT, BCH_FEATURE_INCOMPAT_LARGE_BUCKET,
		"large_bucket"},
	{0, 0, 0 },
};

#define compose_feature_string(type, header)				\
({									\
	struct feature *f;						\
	bool first = true;						\
									\
	for (f = &feature_list[0]; f->compat != 0; f++) {		\
		if (f->compat != BCH_FEATURE_ ## type)			\
			continue;					\
		if (!(BCH_HAS_ ## type ## _FEATURE(sb, f->mask)))	\
			continue;					\
									\
		if (first) {						\
			out += snprintf(out, buf + size - out,		\
					"%s:\t", (header));		\
			first = false;					\
		} else {						\
			out += snprintf(out, buf + size - out, " ");	\
		}							\
									\
		out += snprintf(out, buf + size - out, "%s", f->string);\
									\
	}								\
	if (!first)							\
		out += snprintf(out, buf + size - out, "\n");		\
})

void print_cache_set_supported_feature_sets(struct cache_sb *sb)
{
	char buf[4096];
	char *out;
	int size = sizeof(buf) - 1;

	out = buf;
	memset(buf, 0, sizeof(buf));
	compose_feature_string(COMPAT, "sb.feature_compat");
	printf("%s", buf);

	out = buf;
	memset(buf, 0, sizeof(buf));
	compose_feature_string(RO_COMPAT, "sb.feature_ro_compat");
	printf("%s", buf);

	out = buf;
	memset(buf, 0, sizeof(buf));
	compose_feature_string(INCOMPAT, "sb.feature_incompat");
	printf("%s", buf);
}
