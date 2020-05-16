// SPDX-License-Identifier: GPL-2.0
/*
 * This file is part of bcache tools.
 * Copyright (c) 2020 SUSE Software Solutions
 *
 * Authors: Coly Li <colyli@suse.de>
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>

#include "bcache.h"

/*
 * copied and modified from zonefs_get_dev_capacity() of
 * zonefs-tools.
 * returns   0: zone size 0 indicates a non-zoned device
 *         > 0: actual zone size of the zoned device
 */
static size_t get_zone_size(char *devname)
{
	char str[128];
	FILE *file;
	int res;
	size_t zone_size = 0;

	snprintf(str, sizeof(str),
		"/sys/block/%s/queue/chunk_sectors",
		basename(devname));
	file = fopen(str, "r");
	if (!file)
		goto out;

	memset(str, 0, sizeof(str));
	res = fscanf(file, "%s", str);
	fclose(file);

	if (res != 1)
		goto out;

	zone_size = atol(str);

out:
	return zone_size;
}

/*
 * Update data_offset for zoned device, if the backing
 * device is a zoned device,
 * - just leave whole zone 0 to bcache super block on
 *   backing device.
 * - if data_offset is specified and larger than
 *   BDEV_DATA_START_DEFAULT, then it should be a zone
 *   size aligned value.
 */
void check_data_offset_for_zoned_device(char *devname,
				       uint64_t *data_offset)
{
	uint64_t _data_offset = *data_offset;
	size_t zone_size = get_zone_size(devname);

	if (!zone_size)
		return;

	if (!_data_offset ||
	    (_data_offset == BDEV_DATA_START_DEFAULT &&
	     zone_size > BDEV_DATA_START_DEFAULT))
		_data_offset = zone_size;

	if (_data_offset < zone_size) {
		fprintf(stderr,
			"data_offset %lu should be larger than zone_size %lu for zoned device %s\n",
			_data_offset, zone_size, devname);
		exit(EXIT_FAILURE);
	}

	if (_data_offset & (zone_size - 1)) {
		fprintf(stderr,
			"data_offset %lu is not aligned to zone size %lu for zoned device %s\n",
			_data_offset, zone_size, devname);
	}

	*data_offset = _data_offset;
}
