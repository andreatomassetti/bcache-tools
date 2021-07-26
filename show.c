// SPDX-License-Identifier: GPL-2.0

#define _FILE_OFFSET_BITS       64
#define __USE_FILE_OFFSET64
#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <locale.h>
#include <limits.h>
#include <assert.h>
#include <blkid/blkid.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <uuid/uuid.h>

#include "bcache.h"
#include "lib.h"
#include "bitwise.h"
#include "zoned.h"
#include "features.h"
#include "list.h"

int show_bdevs_detail(void)
{
	struct list_head head;
	struct dev *devs, *n;

	INIT_LIST_HEAD(&head);
	int ret;

	ret = list_bdevs(&head);
	if (ret != 0) {
		fprintf(stderr, "Failed to list devices\n");
		return ret;
	}
	printf("Name\t\tUuid\t\t\t\t\tCset_Uuid\t\t\t\tType\t\t\tState");
	printf("\t\t\tBname\t\tAttachToDev\tAttachToCset\n");
	list_for_each_entry_safe(devs, n, &head, dev_list) {
		printf("%s\t%s\t%s\t%lu", devs->name, devs->uuid,
		       devs->cset, devs->version);
		switch (devs->version) {
			// These are handled the same by the kernel
		case BCACHE_SB_VERSION_CDEV:
		case BCACHE_SB_VERSION_CDEV_WITH_UUID:
		case BCACHE_SB_VERSION_CDEV_WITH_FEATURES:
			printf(" (cache)");
			break;
			// The second adds data offset supporet
		case BCACHE_SB_VERSION_BDEV:
		case BCACHE_SB_VERSION_BDEV_WITH_OFFSET:
		case BCACHE_SB_VERSION_BDEV_WITH_FEATURES:
			printf(" (data)");
			break;
		default:
			printf(" (unknown)");
			break;
		}
		printf("\t\t%-16s", devs->state);
		printf("\t%-16s", devs->bname);
		char attachdev[30];

		if (strlen(devs->attachuuid) == 36) {
			cset_to_devname(&head, devs->cset, attachdev);
		} else if (devs->version == BCACHE_SB_VERSION_CDEV
			   || devs->version ==
			   BCACHE_SB_VERSION_CDEV_WITH_UUID) {
			strcpy(attachdev, BCACHE_NO_SUPPORT);
		} else {
			strcpy(attachdev, BCACHE_ATTACH_ALONE);
		}
		printf("%-16s", attachdev);
		printf("%s", devs->attachuuid);
		putchar('\n');
	}
	free_dev(&head);
	return 0;
}


int show_bdevs(void)
{
	struct list_head head;
	struct dev *devs, *n;

	INIT_LIST_HEAD(&head);
	int ret;

	ret = list_bdevs(&head);
	if (ret != 0) {
		fprintf(stderr, "Failed to list devices\n");
		return ret;
	}

	printf("Name\t\tType\t\tState\t\t\tBname\t\tAttachToDev\n");
	list_for_each_entry_safe(devs, n, &head, dev_list) {
		printf("%s\t%lu", devs->name, devs->version);
		switch (devs->version) {
			// These are handled the same by the kernel
		case BCACHE_SB_VERSION_CDEV:
		case BCACHE_SB_VERSION_CDEV_WITH_UUID:
		case BCACHE_SB_VERSION_CDEV_WITH_FEATURES:
			printf(" (cache)");
			break;

			// The second adds data offset supporet
		case BCACHE_SB_VERSION_BDEV:
		case BCACHE_SB_VERSION_BDEV_WITH_OFFSET:
		case BCACHE_SB_VERSION_BDEV_WITH_FEATURES:
			printf(" (data)");
			break;

		default:
			printf(" (unknown)");
			break;
		}

		printf("\t%-16s", devs->state);
		printf("\t%-16s", devs->bname);

		char attachdev[30];

		if (strlen(devs->attachuuid) == 36) {
			cset_to_devname(&head, devs->cset, attachdev);
		} else if (devs->version == BCACHE_SB_VERSION_CDEV
			   || devs->version ==
			   BCACHE_SB_VERSION_CDEV_WITH_UUID) {
			strcpy(attachdev, BCACHE_NO_SUPPORT);
		} else {
			strcpy(attachdev, BCACHE_ATTACH_ALONE);
		}
		printf("%s", attachdev);
		putchar('\n');
	}
	free_dev(&head);
	return 0;
}

int detail_single(char *devname)
{
	struct bdev bd;
	struct cdev cd;
	int type = 1;
	int ret;

	ret = detail_dev(devname, &bd, &cd, &type);
	if (ret != 0) {
		fprintf(stderr, "Failed to detail device\n");
		return ret;
	}
	if (type == BCACHE_SB_VERSION_BDEV ||
	    type == BCACHE_SB_VERSION_BDEV_WITH_OFFSET ||
	    type == BCACHE_SB_VERSION_BDEV_WITH_FEATURES) {
		printf("sb.magic\t\t%s\n", bd.base.magic);
		printf("sb.first_sector\t\t%" PRIu64 "\n",
		       bd.base.first_sector);
		printf("sb.csum\t\t\t%" PRIX64 "\n", bd.base.csum);
		printf("sb.version\t\t%" PRIu64, bd.base.version);
		printf(" [backing device]\n");
		putchar('\n');
		printf("dev.label\t\t");
		if (*bd.base.label)
			print_encode(bd.base.label);
		else
			printf("(empty)");
		putchar('\n');
		printf("dev.uuid\t\t%s\n", bd.base.uuid);
		printf("dev.sectors_per_block\t%u\n"
		       "dev.sectors_per_bucket\t%u\n",
		       bd.base.sectors_per_block,
		       bd.base.sectors_per_bucket);
		printf("dev.data.first_sector\t%u\n"
		       "dev.data.cache_mode\t%d",
		       bd.first_sector, bd.cache_mode);
		switch (bd.cache_mode) {
		case CACHE_MODE_WRITETHROUGH:
			printf(" [writethrough]\n");
			break;
		case CACHE_MODE_WRITEBACK:
			printf(" [writeback]\n");
			break;
		case CACHE_MODE_WRITEAROUND:
			printf(" [writearound]\n");
			break;
		case CACHE_MODE_NONE:
			printf(" [no caching]\n");
			break;
		default:
			putchar('\n');
		}
		printf("dev.data.cache_state\t%u", bd.cache_state);
		switch (bd.cache_state) {
		case BDEV_STATE_NONE:
			printf(" [detached]\n");
			break;
		case BDEV_STATE_CLEAN:
			printf(" [clean]\n");
			break;
		case BDEV_STATE_DIRTY:
			printf(" [dirty]\n");
			break;
		case BDEV_STATE_STALE:
			printf(" [inconsistent]\n");
			break;
		default:
			putchar('\n');
		}

		putchar('\n');
		printf("cset.uuid\t\t%s\n", bd.base.cset);
	} else if (type == BCACHE_SB_VERSION_CDEV ||
		   type == BCACHE_SB_VERSION_CDEV_WITH_UUID ||
		   type == BCACHE_SB_VERSION_CDEV_WITH_FEATURES) {
		printf("sb.magic\t\t%s\n", cd.base.magic);
		printf("sb.first_sector\t\t%" PRIu64 "\n",
		       cd.base.first_sector);
		printf("sb.csum\t\t\t%" PRIX64 "\n", cd.base.csum);
		printf("sb.version\t\t%" PRIu64, cd.base.version);
		printf(" [cache device]\n");
		print_cache_set_supported_feature_sets(&cd.base.sb);
		putchar('\n');
		printf("dev.label\t\t");
		if (*cd.base.label)
			print_encode(cd.base.label);
		else
			printf("(empty)");
		putchar('\n');
		printf("dev.uuid\t\t%s\n", cd.base.uuid);
		printf("dev.sectors_per_block\t%u\n"
		       "dev.sectors_per_bucket\t%u\n",
		       cd.base.sectors_per_block,
		       cd.base.sectors_per_bucket);
		printf("dev.cache.first_sector\t%u\n"
		       "dev.cache.cache_sectors\t%ju\n"
		       "dev.cache.total_sectors\t%ju\n"
		       "dev.cache.ordered\t%s\n"
		       "dev.cache.discard\t%s\n"
		       "dev.cache.pos\t\t%u\n"
		       "dev.cache.replacement\t%d",
		       cd.first_sector,
		       cd.cache_sectors,
		       cd.total_sectors,
		       cd.ordered ? "yes" : "no",
		       cd.discard ? "yes" : "no", cd.pos, cd.replacement);
		switch (cd.replacement) {
		case CACHE_REPLACEMENT_LRU:
			printf(" [lru]\n");
			break;
		case CACHE_REPLACEMENT_FIFO:
			printf(" [fifo]\n");
			break;
		case CACHE_REPLACEMENT_RANDOM:
			printf(" [random]\n");
			break;
		default:
			putchar('\n');
		}

		putchar('\n');
		printf("cset.uuid\t\t%s\n", cd.base.cset);
	} else {
		return 1;
	}
	return 0;
}
