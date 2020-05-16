// SPDX-License-Identifier: GPL-2.0
/*
 * This file is part of bcache tools.
 * Copyright (c) 2020 SUSE Software Solutions
 *
 * Authors: Coly Li <colyli@suse.de>
 */
#ifndef __ZONED_H
#define __ZONED_H

void check_data_offset_for_zoned_device(char *devname, uint64_t *data_offset);
int is_zoned_device(char *devname);

#endif
