// SPDX-License-Identifier: GPL-2.0
/*
 * Author: Coly Li <colyli@suse.de>
 *
 * Inspired by e2fsprogs features compat/incompat/ro_compat
 * related code.
 */
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

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
