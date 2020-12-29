// SPDX-License-Identifier: GPL-2.0
// Author: Shaoxiong Li <dahefanteng@gmail.com>

/*
 * Author: Kent Overstreet <kmo@daterainc.com>
 *
 * GPLv2
 *
 * TODO: It's OK to merge this file with 'make-bcache.c'
 * after the 'bcache' tool has been well tested.
 */

#define _FILE_OFFSET_BITS	64
#define __USE_FILE_OFFSET64
#define _XOPEN_SOURCE 600

#include <blkid/blkid.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/fs.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <uuid/uuid.h>

#include "bcache.h"
#include "lib.h"
#include "bitwise.h"
#include "zoned.h"

#define max(x, y) ({				\
	typeof(x) _max1 = (x);			\
	typeof(y) _max2 = (y);			\
	(void) (&_max1 == &_max2);		\
	_max1 > _max2 ? _max1 : _max2; })

uint64_t getblocks(int fd)
{
	uint64_t ret;
	struct stat statbuf;

	if (fstat(fd, &statbuf)) {
		perror("stat error\n");
		exit(EXIT_FAILURE);
	}
	ret = statbuf.st_size / 512;
	if (S_ISBLK(statbuf.st_mode))
		if (ioctl(fd, BLKGETSIZE, &ret)) {
			perror("ioctl error");
			exit(EXIT_FAILURE);
		}
	return ret;
}

uint64_t hatoi(const char *s)
{
	char *e;
	long long i = strtoll(s, &e, 10);

	switch (*e) {
	case 't':
	case 'T':
		i *= 1024;
	case 'g':
	case 'G':
		i *= 1024;
	case 'm':
	case 'M':
		i *= 1024;
	case 'k':
	case 'K':
		i *= 1024;
	}
	return i;
}

unsigned int hatoi_validate(const char *s,
			    const char *msg,
			    unsigned long max)
{
	uint64_t v = hatoi(s);

	if (v & (v - 1)) {
		fprintf(stderr, "%s must be a power of two\n", msg);
		exit(EXIT_FAILURE);
	}

	v /= 512;

	if (v > max) {
		fprintf(stderr, "%s too large\n", msg);
		exit(EXIT_FAILURE);
	}

	if (!v) {
		fprintf(stderr, "%s too small\n", msg);
		exit(EXIT_FAILURE);
	}

	return v;
}

char *skip_spaces(const char *str)
{
	while (isspace(*str))
		++str;
	return (char *)str;
}

char *strim(char *s)
{
	size_t size;
	char *end;

	s = skip_spaces(s);
	size = strlen(s);
	if (!size)
		return s;

	end = s + size - 1;
	while (end >= s && isspace(*end))
		end--;
	*(end + 1) = '\0';

	return s;
}

ssize_t read_string_list(const char *buf, const char * const list[])
{
	size_t i;
	char *s, *d = strdup(buf);

	if (!d)
		return -ENOMEM;

	s = strim(d);

	for (i = 0; list[i]; i++)
		if (!strcmp(list[i], s))
			break;

	free(d);

	if (!list[i])
		return -EINVAL;

	return i;
}

void usage(void)
{
	fprintf(stderr,
		   "Usage: make-bcache [options] device\n"
	       "	-C, --cache		Format a cache device\n"
	       "	-B, --bdev		Format a backing device\n"
	       "	-b, --bucket		bucket size\n"
	       "	-w, --block		block size (hard sector size of SSD, often 2k)\n"
	       "	-o, --data-offset	data offset in sectors\n"
	       "	    --cset-uuid		UUID for the cache set\n"
//	       "	-U			UUID\n"
	       "	    --writeback		enable writeback\n"
	       "	    --discard		enable discards\n"
	       "	    --force		reformat a bcache device even if it is running\n"
	       "	-l, --label		set label for device\n"
	       "	    --cache_replacement_policy=(lru|fifo)\n"
	       "	-h, --help		display this help and exit\n");
	exit(EXIT_FAILURE);
}

const char * const cache_replacement_policies[] = {
	"lru",
	"fifo",
	"random",
	NULL
};

int blkdiscard_all(char *path, int fd)
{
	printf("%s blkdiscard beginning...", path);
	fflush(stdout);

	uint64_t end, blksize, secsize, range[2];
	struct stat sb;

	range[0] = 0;
	range[1] = ULLONG_MAX;

	if (fstat(fd, &sb) == -1)
		goto err;

	if (!S_ISBLK(sb.st_mode))
		goto err;

	if (ioctl(fd, BLKGETSIZE64, &blksize))
		goto err;

	if (ioctl(fd, BLKSSZGET, &secsize))
		goto err;

	/* align range to the sector size */
	range[0] = (range[0] + secsize - 1) & ~(secsize - 1);
	range[1] &= ~(secsize - 1);

	/* is the range end behind the end of the device ?*/
	end = range[0] + range[1];
	if (end < range[0] || end > blksize)
		range[1] = blksize - range[0];

	if (ioctl(fd, BLKDISCARD, &range))
		goto err;

	printf("done\n");
	return 0;
err:
	printf("\r                                ");
	return -1;
}

static void write_sb(char *dev, unsigned int block_size,
			unsigned int bucket_size,
			bool writeback, bool discard, bool wipe_bcache,
			unsigned int cache_replacement_policy,
			uint64_t data_offset,
			uuid_t set_uuid, bool bdev, bool force, char *label)
{
	int fd;
	char uuid_str[40], set_uuid_str[40], zeroes[SB_START] = {0};
	struct cache_sb_disk sb_disk;
	struct cache_sb sb;
	blkid_probe pr;

	fd = open(dev, O_RDWR|O_EXCL);

	if (fd == -1) {
		if ((errno == 16) && force) {
			struct bdev bd;
			struct cdev cd;
			int type = 1;
			int ret;

			ret = detail_dev(dev, &bd, &cd, &type);
			if (ret != 0)
				exit(EXIT_FAILURE);
			if (type == BCACHE_SB_VERSION_BDEV) {
				ret = stop_backdev(dev);
			} else if (type == BCACHE_SB_VERSION_CDEV
				|| type == BCACHE_SB_VERSION_CDEV_WITH_UUID) {
				ret = unregister_cset(cd.base.cset);
			} else {
				fprintf(stderr,
					"%s,And this is not a bcache device.\n",
					strerror(errno));
				exit(EXIT_FAILURE);
			}
			if (ret != 0)
				exit(EXIT_FAILURE);
			int i;
			bool opened;

			for (i = 0; i < 3; i++) {
				sleep(3);
				fd = open(dev, O_RDWR|O_EXCL);
				if (fd == -1) {
					fprintf(stdout,
						"Waiting for bcache device to be closed.\n");
				} else {
					opened = true;
					break;
				}
			}
			if (!opened) {
				fprintf(stderr,
					"Bcache devices has not completely closed,");
				fprintf(stderr, "you can try it sooner.\n");
				exit(EXIT_FAILURE);
			}
		} else {
			fprintf(stderr, "Can't open dev %s: %s\n",
					dev, strerror(errno));
			exit(EXIT_FAILURE);
		}
	}

	if (force)
		wipe_bcache = true;

	if (pread(fd, &sb_disk, sizeof(sb_disk), SB_START) != sizeof(sb_disk))
		exit(EXIT_FAILURE);

	if (!memcmp(sb_disk.magic, bcache_magic, 16)) {
		if (wipe_bcache) {
			if (pwrite(fd, zeroes, sizeof(sb_disk),
				SB_START) != sizeof(sb_disk)) {
				fprintf(stderr,
					"Failed to erase super block for %s\n",
					dev);
				exit(EXIT_FAILURE);
			}
		} else {
			fprintf(stderr, "Already a bcache device on %s,", dev);
			fprintf(stderr,
				"overwrite with --wipe-bcache or --force\n");
			exit(EXIT_FAILURE);
		}
	}
	pr = blkid_new_probe();
	if (!pr)
		exit(EXIT_FAILURE);
	if (blkid_probe_set_device(pr, fd, 0, 0))
		exit(EXIT_FAILURE);
	/* enable ptable probing; superblock probing is enabled by default */
	if (blkid_probe_enable_partitions(pr, true))
		exit(EXIT_FAILURE);
	if (!blkid_do_probe(pr)) {
		/* XXX wipefs doesn't know how to remove partition tables */
		fprintf(stderr,
			"Device %s already has a non-bcache superblock,", dev);
		fprintf(stderr,	"remove it using wipefs and wipefs -a\n");
		exit(EXIT_FAILURE);
	}

	memset(&sb_disk, 0, sizeof(struct cache_sb_disk));
	memset(&sb, 0, sizeof(struct cache_sb));

	sb.offset	= SB_SECTOR;
	sb.version	= bdev
		? BCACHE_SB_VERSION_BDEV
		: BCACHE_SB_VERSION_CDEV;

	memcpy(sb.magic, bcache_magic, 16);
	uuid_generate(sb.uuid);
	memcpy(sb.set_uuid, set_uuid, sizeof(sb.set_uuid));

	sb.block_size	= block_size;

	uuid_unparse(sb.uuid, uuid_str);
	uuid_unparse(sb.set_uuid, set_uuid_str);

	if (SB_IS_BDEV(&sb)) {
		SET_BDEV_CACHE_MODE(&sb, writeback ?
			CACHE_MODE_WRITEBACK : CACHE_MODE_WRITETHROUGH);

		/*
		 * Currently bcache does not support writeback mode for
		 * zoned device as backing device. If the cache mode is
		 * explicitly set to writeback, automatically convert to
		 * writethough mode.
		 */
		if (is_zoned_device(dev) &&
		    BDEV_CACHE_MODE(&sb) == CACHE_MODE_WRITEBACK) {
			printf("Zoned device %s detected: convert to writethrough mode.\n\n",
				dev);
			SET_BDEV_CACHE_MODE(&sb, CACHE_MODE_WRITETHROUGH);
		}

		if (data_offset != BDEV_DATA_START_DEFAULT) {
			if (sb.version < BCACHE_SB_VERSION_BDEV_WITH_OFFSET)
				sb.version = BCACHE_SB_VERSION_BDEV_WITH_OFFSET;
			sb.data_offset = data_offset;
		}

		printf("Name			%s\n", dev);
		printf("Label			%s\n", label);
		printf("Type			data\n");
		printf("UUID:			%s\n"
		       "Set UUID:		%s\n"
		       "version:		%u\n"
		       "block_size_in_sectors:	%u\n"
		       "data_offset_in_sectors:	%ju\n",
		       uuid_str, set_uuid_str,
		       (unsigned int) sb.version,
		       sb.block_size,
		       data_offset);
		putchar('\n');
	} else {
		set_bucket_size(&sb, bucket_size);

		sb.nbuckets		= getblocks(fd) / sb.bucket_size;
		sb.nr_in_set		= 1;
		/* 23 is (SB_SECTOR + SB_SIZE) - 1 sectors */
		sb.first_bucket		= (23 / sb.bucket_size) + 1;

		if (sb.nbuckets < 1 << 7) {
			fprintf(stderr, "Not enough buckets: %llu, need %u\n",
			       sb.nbuckets, 1 << 7);
			exit(EXIT_FAILURE);
		}

		SET_CACHE_DISCARD(&sb, discard);
		SET_CACHE_REPLACEMENT(&sb, cache_replacement_policy);

		printf("Name			%s\n", dev);
		printf("Label			%s\n", label);
		printf("Type			cache\n");
		printf("UUID:			%s\n"
		       "Set UUID:		%s\n"
		       "version:		%u\n"
		       "nbuckets:		%llu\n"
		       "block_size_in_sectors:	%u\n"
		       "bucket_size_in_sectors:	%u\n"
		       "nr_in_set:		%u\n"
		       "nr_this_dev:		%u\n"
		       "first_bucket:		%u\n",
		       uuid_str, set_uuid_str,
		       (unsigned int) sb.version,
		       sb.nbuckets,
		       sb.block_size,
		       sb.bucket_size,
		       sb.nr_in_set,
		       sb.nr_this_dev,
		       sb.first_bucket);

		/* Attempting to discard cache device
		 */
		blkdiscard_all(dev, fd);
		putchar('\n');
	}

	/* write label */
	int num, i;

	num = strlen(label);
	for (i = 0; i < num; i++)
		sb.label[i] = label[i];
	sb.label[i] = '\0';

	/*
	 * Swap native bytes order to little endian for writing
	 * the super block out.
	 */
	to_cache_sb_disk(&sb_disk, &sb);

	/* write csum */
	sb_disk.csum = cpu_to_le64(csum_set(&sb_disk));
	/* Zero start of disk */
	if (pwrite(fd, zeroes, SB_START, 0) != SB_START) {
		perror("write error\n");
		exit(EXIT_FAILURE);
	}
	/* Write superblock */
	if (pwrite(fd, &sb_disk, sizeof(sb_disk), SB_START) != sizeof(sb_disk)) {
		perror("write error\n");
		exit(EXIT_FAILURE);
	}

	fsync(fd);
	close(fd);
}

static unsigned int get_blocksize(const char *path)
{
	struct stat statbuf;

	if (stat(path, &statbuf)) {
		fprintf(stderr, "Error statting %s: %s\n",
			path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (S_ISBLK(statbuf.st_mode)) {
		/* check IO limits:
		 * BLKALIGNOFF: alignment_offset
		 * BLKPBSZGET: physical_block_size
		 * BLKSSZGET: logical_block_size
		 * BLKIOMIN: minimum_io_size
		 * BLKIOOPT: optimal_io_size
		 *
		 * It may be tempting to use physical_block_size,
		 * or even minimum_io_size.
		 * But to be as transparent as possible,
		 * we want to use logical_block_size.
		 */
		unsigned int logical_block_size;
		int fd = open(path, O_RDONLY);

		if (fd < 0) {
			fprintf(stderr, "open(%s) failed: %m\n", path);
			exit(EXIT_FAILURE);
		}
		if (ioctl(fd, BLKSSZGET, &logical_block_size)) {
			fprintf(stderr,
				"ioctl(%s, BLKSSZGET) failed: %m\n", path);
			exit(EXIT_FAILURE);
		}
		close(fd);
		return logical_block_size / 512;

	}
	return statbuf.st_blksize / 512;
}

int make_bcache(int argc, char **argv)
{
	int c, bdev = -1;
	unsigned int i, ncache_devices = 0, nbacking_devices = 0;
	char *cache_devices[argc];
	char *backing_devices[argc];
	char label[SB_LABEL_SIZE] = { 0 };
	unsigned int block_size = 0, bucket_size = 1024;
	int writeback = 0, discard = 0, wipe_bcache = 0, force = 0;
	unsigned int cache_replacement_policy = 0;
	uint64_t data_offset = BDEV_DATA_START_DEFAULT;
	uuid_t set_uuid;

	uuid_generate(set_uuid);

	struct option opts[] = {
		{ "cache",		0, NULL,	'C' },
		{ "bdev",		0, NULL,	'B' },
		{ "bucket",		1, NULL,	'b' },
		{ "block",		1, NULL,	'w' },
		{ "writeback",		0, &writeback,	1 },
		{ "wipe-bcache",	0, &wipe_bcache,	1 },
		{ "discard",		0, &discard,	1 },
		{ "cache_replacement_policy", 1, NULL, 'p' },
		{ "cache-replacement-policy", 1, NULL, 'p' },
		{ "data_offset",	1, NULL,	'o' },
		{ "data-offset",	1, NULL,	'o' },
		{ "cset-uuid",		1, NULL,	'u' },
		{ "help",		0, NULL,	'h' },
		{ "force",		0, &force,	 1 },
		{ "label",		1, NULL,	 'l' },
		{ NULL,			0, NULL,	0 },
	};

	while ((c = getopt_long(argc, argv,
				"-hCBUo:w:b:l:",
				opts, NULL)) != -1)
		switch (c) {
		case 'C':
			bdev = 0;
			break;
		case 'B':
			bdev = 1;
			break;
		case 'b':
			bucket_size =
				hatoi_validate(optarg, "bucket size", UINT_MAX);
			break;
		case 'w':
			block_size =
				hatoi_validate(optarg, "block size", USHRT_MAX);
			break;
#if 0
		case 'U':
			if (uuid_parse(optarg, sb.uuid)) {
				fprintf(stderr, "Bad uuid\n");
				exit(EXIT_FAILURE);
			}
			break;
#endif
		case 'p':
			cache_replacement_policy = read_string_list(optarg,
						    cache_replacement_policies);
			break;
		case 'o':
			data_offset = atoll(optarg);
			if (data_offset < BDEV_DATA_START_DEFAULT) {
				fprintf(stderr,
					"Bad data offset; minimum %d sectors\n",
				       BDEV_DATA_START_DEFAULT);
				exit(EXIT_FAILURE);
			}
			break;
		case 'u':
			if (uuid_parse(optarg, set_uuid)) {
				fprintf(stderr, "Bad uuid\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'l':
			if (strlen(optarg) >= SB_LABEL_SIZE) {
				fprintf(stderr, "Label is too long\n");
				exit(EXIT_FAILURE);
			}
			strcpy(label, optarg);
			break;
		case 'h':
			usage();
			break;
		case 1:
			if (bdev == -1) {
				fprintf(stderr, "Please specify -C or -B\n");
				exit(EXIT_FAILURE);
			}

			if (bdev)
				backing_devices[nbacking_devices++] = optarg;
			else
				cache_devices[ncache_devices++] = optarg;
			break;
		}

	if (!ncache_devices && !nbacking_devices) {
		fprintf(stderr, "Please supply a device\n");
		usage();
	}

	if (ncache_devices > 1) {
		fprintf(stderr, "Please specify only one cache device\n");
		usage();
	}

	if (bucket_size < block_size) {
		fprintf(stderr,
			"Bucket size cannot be smaller than block size\n");
		exit(EXIT_FAILURE);
	}

	if (!block_size) {
		for (i = 0; i < ncache_devices; i++)
			block_size = max(block_size,
					 get_blocksize(cache_devices[i]));

		for (i = 0; i < nbacking_devices; i++)
			block_size = max(block_size,
					 get_blocksize(backing_devices[i]));
	}

	for (i = 0; i < ncache_devices; i++)
		write_sb(cache_devices[i], block_size, bucket_size,
			 writeback, discard, wipe_bcache,
			 cache_replacement_policy,
			 data_offset, set_uuid, false, force, label);

	for (i = 0; i < nbacking_devices; i++) {
		check_data_offset_for_zoned_device(backing_devices[i],
						   &data_offset);

		write_sb(backing_devices[i], block_size, bucket_size,
			 writeback, discard, wipe_bcache,
			 cache_replacement_policy,
			 data_offset, set_uuid, true, force, label);
	}

	return 0;
}
