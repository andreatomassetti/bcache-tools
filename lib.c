// SPDX-License-Identifier: GPL-2.0
// Author: Shaoxiong Li <dahefanteng@gmail.com>

#include <stdbool.h>
#include <blkid.h>
#include <dirent.h>
#include <limits.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <uuid.h>
#include <string.h>
#include <malloc.h>
#include <regex.h>

#include "bcache.h"
#include "lib.h"
#include "bitwise.h"
/*
 * utils function
 */

static unsigned int log2_u32(uint32_t n)
{
	int r = 0;

	n = n >> 1;
	while (n) {
		n = n >> 1;
		r++;
	}

	return r;
}

static void trim_prefix(char *dest, char *src, int num)
{
	strcpy(dest, src + num);
}

static void get_tail(char *dest, char *src, int n)
{
	int num, i;

	num = strlen(src);
	for (i = 0; i < n; i++)
		dest[i] = src[num - n + i];
	dest[i] = '\0';
}

static void get_tail_compoment(char *dest, char *src)
{
	int num = strlen(src);
	int i, index = 0;

	for (i = 0; i < num; i++) {
		if (src[i] == '/')
			index = i + 1;
	}
	strcpy(dest, src + index);
}

static void trim_tail(char *src, int n)
{
	int num;

	num = strlen(src);
	src[num - n] = '\0';
}

bool prefix_with(char *dst, char *prefix)
{
	int length = strlen(prefix);

	if (length > strlen(dst))
		return false;
	int i;

	for (i = 0; i < length; i++) {
		if (prefix[i] != dst[i])
			return false;
	}
	return true;
}

bool part_of_disk(char *devname, char *partname)
{
	char pattern[40];
	int status;
	regmatch_t regmatche;
	regex_t reg;

	sprintf(pattern, "^%s.*[0-9]$", devname);
	if (regcomp(&reg, pattern, REG_EXTENDED) != 0)
		fprintf(stderr, "Error happen when compile reg\n");
	status = regexec(&reg, partname, 1, &regmatche, 0);
	regfree(&reg);
	if (status == REG_NOMATCH)
		return false;
	else
		return true;
}

int find_location(char *location, char *devname)
{
	char path[300];
	DIR *blockdir, *bcachedir, *partdir = NULL;
	struct dirent *ptr;

	blockdir = opendir("/sys/block");
	if (blockdir == NULL) {
		fprintf(stderr, "Failed to open dir /sys/block/\n");
		return 1;
	}
	sprintf(path, "/sys/block/%s/bcache", devname);
	bcachedir = opendir(path);
	if (bcachedir != NULL) {
		strcpy(location, devname);
		closedir(bcachedir);
		return 0;
	}
	while ((ptr = readdir(blockdir)) != NULL) {
		if (prefix_with(devname, ptr->d_name)) {
			sprintf(path, "/sys/block/%s/%s", ptr->d_name,
				devname);
			partdir = opendir(path);
			if (partdir != NULL) {
				sprintf(location, "%s/%s", ptr->d_name,
					devname);
				closedir(partdir);
				break;
			}
		}
	}
	closedir(blockdir);
	return 0;
}


int get_backdev_state(char *devname, char *state)
{
	FILE *fd;
	int ret;
	char path[150];
	char location[100] = "";
	char buf[40];

	trim_prefix(buf, devname, DEV_PREFIX_LEN);
	ret = find_location(location, buf);
	if (ret < 0)
		return ret;
	sprintf(path, "/sys/block/%s/bcache/state", location);
	fd = fopen(path, "r");
	if (fd == NULL) {
		strcpy(state, BCACHE_BASIC_STATE_INACTIVE);
		return 0;
	}
	int i = 0;

	while ((state[i] = getc(fd)) != '\n')
		i++;
	state[i] = '\0';
	fclose(fd);

	int fd_run;

	sprintf(path, "/sys/block/%s/bcache/running", location);
	fd_run = open(path, O_RDONLY);
	if (fd_run < 0) {
		fprintf(stderr,
			"Failed to open %s\n", path);
		return 1;
	}

	char running[20];
	int num;

	num = read(fd_run, running, 10);
	if (num < 0) {
		fprintf(stderr, "Failed to fetch running infomation\n");
		close(fd_run);
		return 1;
	}
	close(fd_run);
	if (running[0] == '1')
		strcat(state, "(running)");
	return 0;
}

int get_cachedev_state(char *cset_id, char *state)
{
	DIR *dir = NULL;
	char path[100];

	sprintf(path, "/sys/fs/bcache/%s/", cset_id);
	dir = opendir(path);
	if (dir == NULL)
		strcpy(state, BCACHE_BASIC_STATE_INACTIVE);
	else
		strcpy(state, BCACHE_BASIC_STATE_ACTIVE);
	closedir(dir);
	return 0;
}

int get_state(struct dev *dev, char *state)
{
	if (dev->version == BCACHE_SB_VERSION_CDEV ||
	    dev->version == BCACHE_SB_VERSION_CDEV_WITH_UUID ||
	    dev->version == BCACHE_SB_VERSION_CDEV_WITH_FEATURES)
		return get_cachedev_state(dev->cset, state);
	else if (dev->version == BCACHE_SB_VERSION_BDEV ||
		 dev->version == BCACHE_SB_VERSION_BDEV_WITH_OFFSET ||
		 dev->version == BCACHE_SB_VERSION_BDEV_WITH_FEATURES)
		return get_backdev_state(dev->name, state);
	else
		return 1;
}


int get_dev_bname(char *devname, char *bname)
{
	int ret;
	char path[150];
	char location[100] = "";
	char buf[40];
	char link[100];

	trim_prefix(buf, devname, DEV_PREFIX_LEN);
	ret = find_location(location, buf);
	if (ret < 0)
		return ret;
	sprintf(path, "/sys/block/%s/bcache/dev", location);
	ret = readlink(path, link, sizeof(link));
	if (ret < 0)
		strcpy(bname, BCACHE_BNAME_NOT_EXIST);
	else {
		trim_tail(link, strlen(link) - ret);
		get_tail_compoment(bname, link);
	}
	return 0;
}

int get_bname(struct dev *dev, char *bname)
{
	if (dev->version == BCACHE_SB_VERSION_CDEV
	    || dev->version == BCACHE_SB_VERSION_CDEV_WITH_UUID)
		strcpy(bname, BCACHE_NO_SUPPORT);
	else if (dev->version == BCACHE_SB_VERSION_BDEV
		   || dev->version == BCACHE_SB_VERSION_BDEV_WITH_OFFSET)
		return get_dev_bname(dev->name, bname);
	return 0;
}

int get_backdev_attachpoint(char *devname, char *point)
{
	int ret;
	char path[150];
	char location[100] = "";
	char buf[20];
	char link[100];
	char uuid[40];

	trim_prefix(buf, devname, DEV_PREFIX_LEN);
	ret = find_location(location, buf);
	if (ret < 0)
		return ret;
	sprintf(path, "/sys/block/%s/bcache/cache", location);
	ret = readlink(path, link, sizeof(link));
	if (ret < 0)
		strcpy(point, BCACHE_BNAME_NOT_EXIST);
	else {
		trim_tail(link, strlen(link) - ret);
		get_tail(uuid, link, 36);
		strcpy(point, uuid);
	}
	return 0;
}

int get_point(struct dev *dev, char *point)
{
	if (dev->version == BCACHE_SB_VERSION_CDEV
	    || dev->version == BCACHE_SB_VERSION_CDEV_WITH_UUID)
		strcpy(point, BCACHE_NO_SUPPORT);
	else if (dev->version == BCACHE_SB_VERSION_BDEV
		   || dev->version == BCACHE_SB_VERSION_BDEV_WITH_OFFSET)
		return get_backdev_attachpoint(dev->name, point);
	return 0;
}

int cset_to_devname(struct list_head *head, char *cset, char *devname)
{
	struct dev *dev;

	list_for_each_entry(dev, head, dev_list) {
		if ((dev->version == BCACHE_SB_VERSION_CDEV
		     || dev->version == BCACHE_SB_VERSION_CDEV_WITH_UUID)
		    && strcmp(dev->cset, cset) == 0)
			strcpy(devname, dev->name);
	}
	return 0;
}


int detail_base(char *devname, struct cache_sb sb, struct dev *base)
{
	int ret;

	base->sb = sb;
	strcpy(base->name, devname);
	base->magic = "ok";
	base->first_sector = SB_SECTOR;
	base->version = sb.version;

	strncpy(base->label, (char *) sb.label, SB_LABEL_SIZE);
	base->label[SB_LABEL_SIZE] = '\0';

	uuid_unparse(sb.uuid, base->uuid);
	uuid_unparse(sb.set_uuid, base->cset);
	base->sectors_per_block = sb.block_size;
	base->sectors_per_bucket = sb.bucket_size;
	ret = get_state(base, base->state);
	if (ret != 0) {
		fprintf(stderr, "Failed to get state for %s\n", devname);
		return ret;
	}
	ret = get_bname(base, base->bname);
	if (ret != 0) {
		fprintf(stderr, "Failed to get bname for %s\n", devname);
		return ret;
	}
	ret = get_point(base, base->attachuuid);
	if (ret != 0) {
		fprintf(stderr, "Failed to get attachuuid for  %s\n",
			devname);
		return ret;
	}
	return 0;
}


int may_add_item(char *devname, struct list_head *head)
{
	struct cache_sb_disk sb_disk;
	struct cache_sb sb;

	if (strcmp(devname, ".") == 0 || strcmp(devname, "..") == 0)
		return 0;
	char dev[261];

	sprintf(dev, "/dev/%s", devname);
	int fd = open(dev, O_RDONLY);

	if (fd == -1)
		return 0;
	if (pread(fd, &sb_disk, sizeof(sb_disk), SB_START) != sizeof(sb_disk)) {
		close(fd);
		return 0;
	}

	to_cache_sb(&sb, &sb_disk);

	if (memcmp(sb.magic, bcache_magic, 16)) {
		close(fd);
		return 0;
	}
	struct dev *tmp;
	int ret;

	tmp = (struct dev *) malloc(DEVLEN);

	tmp->csum = le64_to_cpu(sb_disk.csum);
	ret = detail_base(dev, sb, tmp);
	if (ret != 0) {
		fprintf(stderr, "Failed to get information for %s\n", dev);
		return 1;
	}
	list_add_tail(&tmp->dev_list, head);
	return 0;
}

int list_bdevs(struct list_head *head)
{
	int ret;
	DIR *dir, *subdir;
	struct dirent *ptr, *subptr;
	char path[300];

	dir = opendir("/sys/block");
	if (dir == NULL) {
		fprintf(stderr, "Unable to open dir /sys/block\n");
		return 1;
	}
	while ((ptr = readdir(dir)) != NULL) {
		if (strcmp(ptr->d_name, ".") == 0
			|| strcmp(ptr->d_name, "..") == 0)
			continue;
		sprintf(path, "/sys/block/%s", ptr->d_name);
		subdir = opendir(path);
		if (subdir == NULL) {
			fprintf(stderr, "Unable to open dir /sys/block\n");
			return 1;
		}
		while ((subptr = readdir(subdir)) != NULL) {
			if (strcmp(subptr->d_name, ".") == 0
				|| strcmp(subptr->d_name, "..") == 0)
				continue;
			if (part_of_disk(ptr->d_name, subptr->d_name)) {
				ret = may_add_item(subptr->d_name, head);
				if (ret != 0)
					return ret;
			}
		}
		ret = may_add_item(ptr->d_name, head);
		if (ret != 0)
			return ret;
	}
	closedir(dir);
	return 0;
}

int detail_dev(char *devname, struct bdev *bd, struct cdev *cd, int *type)
{
	struct cache_sb_disk sb_disk;
	struct cache_sb sb;
	uint64_t expected_csum;
	int fd = open(devname, O_RDONLY);

	if (fd < 0) {
		fprintf(stderr, "Error: Can't open dev  %s\n", devname);
		return 1;
	}

	if (pread(fd, &sb_disk, sizeof(sb_disk), SB_START) != sizeof(sb_disk)) {
		fprintf(stderr, "Couldn't read\n");
		goto Fail;
	}

	to_cache_sb(&sb, &sb_disk);

	if (memcmp(sb.magic, bcache_magic, 16)) {
		fprintf(stderr,
			"Bad magic,make sure this is an bcache device\n");
		goto Fail;
	}

	if (!(sb.offset == SB_SECTOR)) {
		fprintf(stderr, "Invalid superblock (bad sector)\n");
		goto Fail;
	}

	expected_csum = csum_set(&sb_disk);
	if (le64_to_cpu(sb_disk.csum) != expected_csum) {
		fprintf(stderr, "Csum is not match with expected one\n");
		goto Fail;
	}

	/* Check for incompat feature set */
	if (sb.version >= BCACHE_SB_VERSION_BDEV_WITH_FEATURES ||
	    sb.version >= BCACHE_SB_VERSION_CDEV_WITH_FEATURES) {
		uint64_t features;

		features = sb.feature_compat & ~BCH_FEATURE_COMPAT_SUPP;
		if (features) {
			fprintf(stderr,
				"Unsupported compatible feature found\n");
			goto Fail;
		}

		features = sb.feature_ro_compat & ~BCH_FEATURE_RO_COMPAT_SUPP;
		if (features) {
			fprintf(stderr,
				"Unsupported read-only compatible feature found\n");
			goto Fail;
		}

		features = sb.feature_incompat & ~BCH_FEATURE_INCOMPAT_SUPP;
		if (features) {
			fprintf(stderr,
				"Unsupported incompatible feature found\n");
			goto Fail;
		}
	}

	*type = sb.version;
	if (sb.version == BCACHE_SB_VERSION_BDEV ||
	    sb.version == BCACHE_SB_VERSION_BDEV_WITH_OFFSET ||
	    sb.version == BCACHE_SB_VERSION_BDEV_WITH_FEATURES) {
		detail_base(devname, sb, &bd->base);
		bd->first_sector = BDEV_DATA_START_DEFAULT;
		bd->cache_mode = BDEV_CACHE_MODE(&sb);
		bd->cache_state = BDEV_STATE(&sb);
	} else if (sb.version == BCACHE_SB_VERSION_CDEV ||
		   sb.version == BCACHE_SB_VERSION_CDEV_WITH_UUID ||
		   sb.version == BCACHE_SB_VERSION_CDEV_WITH_FEATURES) {
		detail_base(devname, sb, &cd->base);
		cd->first_sector = sb.bucket_size * sb.first_bucket;
		cd->cache_sectors =
		    sb.bucket_size * (sb.nbuckets - sb.first_bucket);
		cd->total_sectors = sb.bucket_size * sb.nbuckets;
		cd->ordered = CACHE_SYNC(&sb);
		cd->discard = CACHE_DISCARD(&sb);
		cd->pos = sb.nr_this_dev;
		cd->replacement = CACHE_REPLACEMENT(&sb);
	} else {
		fprintf(stderr, "Unknown bcache device type found\n");
		goto Fail;
	}
	return 0;
Fail:
	close(fd);
	return 1;
}

int register_dev(char *devname)
{
	int fd;

	fd = open("/sys/fs/bcache/register", O_WRONLY);
	if (fd < 0) {
		perror("Error opening /sys/fs/bcache/register");
		fprintf(stderr,
			"The bcache kernel module must be loaded\n");
		return 1;
	}
	if (dprintf(fd, "%s\n", devname) < 0) {
		fprintf(stderr, "Error registering %s with bcache: %m\n",
			devname);
		close(fd);
		return 1;
	}
	close(fd);
	return 0;
}

int unregister_cset(char *cset)
{
	int fd;
	char path[100];

	sprintf(path, "/sys/fs/bcache/%s/unregister", cset);
	fd = open(path, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "Can't open %s\n", path);
		return 1;
	}
	if (dprintf(fd, "%d\n", 1) < 0) {
		fprintf(stderr, "Failed to unregister this cache device\n");
		close(fd);
		return 1;
	}
	close(fd);
	return 0;
}

int stop_backdev(char *devname)
{
	char path[150];
	char location[100] = "";
	int fd, ret;
	char buf[20];

	trim_prefix(buf, devname, DEV_PREFIX_LEN);
	ret = find_location(location, buf);
	if (ret < 0)
		return ret;
	sprintf(path, "/sys/block/%s/bcache/stop", location);
	fd = open(path, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "Can't open %s\n", path);
		return 1;
	}
	if (dprintf(fd, "%s\n", "1") < 0) {
		fprintf(stderr, "Error stop back device %s\n", devname);
		close(fd);
		return 1;
	}
	close(fd);
	return 0;
}

//preserved for future use
int unregister_both(char *cset)
{
	int fd;
	char path[100];

	sprintf(path, "/sys/fs/bcache/%s/stop", cset);
	fd = open(path, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "Can't open %s\n", path);
		return 1;
	}
	if (dprintf(fd, "%d\n", 1) < 0) {
		fprintf(stderr, "Failed to stop cset and its backends %m\n");
		close(fd);
		return 1;
	}
	close(fd);
	return 0;
}

int attach_backdev(char *cset, char *devname)
{
	int fd, ret;
	char buf[20];
	char location[100] = "";
	char path[150];

	trim_prefix(buf, devname, DEV_PREFIX_LEN);
	ret = find_location(location, buf);
	if (ret < 0)
		return ret;
	sprintf(path, "/sys/block/%s/bcache/attach", location);
	fd = open(path, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "Can't open %s:%m\n", path);
		return 1;
	}
	if (dprintf(fd, "%s\n", cset) < 0) {
		fprintf(stderr, "Failed to attache to cset %s\n", cset);
		close(fd);
		return 1;
	}
	close(fd);
	return 0;
}

int detach_backdev(char *devname)
{
	int fd, ret;
	char buf[20];
	char path[150];
	char location[100] = "";

	trim_prefix(buf, devname, DEV_PREFIX_LEN);
	ret = find_location(location, buf);
	if (ret < 0)
		return ret;
	sprintf(path, "/sys/block/%s/bcache/detach", location);
	fd = open(path, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr,
			"Can't open %s,Make sure the device name is correct\n",
			path);
		return 1;
	}
	if (dprintf(fd, "%d\n", 1) < 0) {
		close(fd);
		fprintf(stderr, "Error detach device %s:%m\n", devname);
		return 1;
	}
	close(fd);
	return 0;
}

int set_backdev_cachemode(char *devname, char *cachemode)
{
	int fd, ret;
	char path[150];
	char location[100] = "";
	char buf[20];

	trim_prefix(buf, devname, DEV_PREFIX_LEN);
	ret = find_location(location, buf);
	if (ret < 0)
		return ret;
	sprintf(path, "/sys/block/%s/bcache/cache_mode", location);
	fd = open(path, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr,
			"Can't open %s,Make sure the device name is correct\n",
			path);
		return 1;
	}
	if (dprintf(fd, "%s\n", cachemode) < 0) {
		fprintf(stderr, "Failed to set cachemode for device %s:%m\n",
		       devname);
		close(fd);
		return 1;
	}
	close(fd);
	return 0;
}

int get_backdev_cachemode(char *devname, char *mode)
{
	int fd, ret;
	char path[150];
	char location[100] = "";

	ret = find_location(location, devname);
	if (ret < 0)
		return ret;
	sprintf(path, "/sys/block/%s/bcache/cache_mode", location);
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		perror("Error opening /sys/fs/bcache/register");
		fprintf(stderr,
			"The bcache kernel module must be loaded\n");
		return 1;
	}
	if (read(fd, mode, 100) < 0) {
		fprintf(stderr, "Failed to fetch device cache mode\n");
		close(fd);
		return 1;
	}
	close(fd);
	return 0;
}

int set_label(char *devname, char *label)
{
	int fd, ret;
	char path[150];
	char location[100] = "";
	char buf[20];

	trim_prefix(buf, devname, DEV_PREFIX_LEN);
	ret = find_location(location, buf);
	if (ret < 0)
		return ret;
	sprintf(path, "/sys/block/%s/bcache/label", location);
	fd = open(path, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr,
			"Please register this device first\n");
		return 1;
	}
	if (dprintf(fd, "%s\n", label) < 0) {
		fprintf(stderr, "Failed to set label for device %s:%m\n",
		       devname);
		close(fd);
		return 1;
	}
	close(fd);
	return 0;
}


struct cache_sb *to_cache_sb(struct cache_sb *sb,
			     struct cache_sb_disk *sb_disk)
{
	/* Convert common part */
	sb->offset = le64_to_cpu(sb_disk->offset);
	sb->version = le64_to_cpu(sb_disk->version);
	memcpy(sb->magic, sb_disk->magic, 16);
	memcpy(sb->uuid, sb_disk->uuid, 16);
	memcpy(sb->set_uuid, sb_disk->set_uuid, 16);
	memcpy(sb->label, sb_disk->label, SB_LABEL_SIZE);
	sb->flags = le64_to_cpu(sb_disk->flags);
	sb->seq = le64_to_cpu(sb_disk->seq);
	sb->block_size = le16_to_cpu(sb_disk->block_size);
	sb->last_mount = le32_to_cpu(sb_disk->last_mount);
	sb->first_bucket = le16_to_cpu(sb_disk->first_bucket);
	sb->keys = le16_to_cpu(sb_disk->keys);

	/* For cache or backing devices*/

	if (sb->version > BCACHE_SB_MAX_VERSION) {
		/* Unsupported version */
		fprintf(stderr, "Unsupported super block version: %lld\n",
			sb->version);
	} else if (SB_IS_BDEV(sb)) {
		/* Backing device */
		sb->data_offset = le64_to_cpu(sb_disk->data_offset);
	} else {
		int i;

		/* Cache device */
		sb->nbuckets = le64_to_cpu(sb_disk->nbuckets);
		sb->nr_in_set = le16_to_cpu(sb_disk->nr_in_set);
		sb->nr_this_dev = le16_to_cpu(sb_disk->nr_this_dev);
		sb->bucket_size = le32_to_cpu(sb_disk->bucket_size);

		for (i = 0; i < SB_JOURNAL_BUCKETS; i++)
			sb->d[i]= le64_to_cpu(sb_disk->d[i]);
	}

	if (sb->version >= BCACHE_SB_VERSION_CDEV_WITH_FEATURES) {
		sb->feature_compat = le64_to_cpu(sb_disk->feature_compat);
		sb->feature_incompat = le64_to_cpu(sb_disk->feature_incompat);
		sb->feature_ro_compat = le64_to_cpu(sb_disk->feature_ro_compat);
	}

	if (sb->version >= BCACHE_SB_VERSION_CDEV_WITH_FEATURES) {
		if (bch_has_feature_large_bucket(sb))
			sb->bucket_size = 1 << le16_to_cpu(sb_disk->bucket_size);
		else if (bch_has_feature_obso_large_bucket(sb))
			sb->bucket_size +=
				le16_to_cpu(sb_disk->obso_bucket_size_hi) << 16;
	}

	return sb;
}

struct cache_sb_disk *to_cache_sb_disk(struct cache_sb_disk *sb_disk,
				       struct cache_sb *sb)
{
	/* Convert common part */
	sb_disk->offset = cpu_to_le64(sb->offset);
	sb_disk->version = cpu_to_le64(sb->version);
	memcpy(sb_disk->magic, sb->magic, 16);
	memcpy(sb_disk->uuid, sb->uuid, 16);
	memcpy(sb_disk->set_uuid, sb->set_uuid, 16);
	memcpy(sb_disk->label, sb->label, SB_LABEL_SIZE);
	sb_disk->flags = cpu_to_le64(sb->flags);
	sb_disk->seq = cpu_to_le64(sb->seq);
	sb_disk->block_size = cpu_to_le16(sb->block_size);
	sb_disk->last_mount = cpu_to_le32(sb->last_mount);
	sb_disk->first_bucket = cpu_to_le16(sb->first_bucket);
	sb_disk->keys = cpu_to_le16(sb->keys);

	/* For cache and backing devices */

	if (sb->version > BCACHE_SB_MAX_VERSION) {
		/* Unsupported version */
		fprintf(stderr, "Unsupported super block version: %lld\n",
			sb->version);
	} else if (SB_IS_BDEV(sb)) {
		/* Backing device */
		sb_disk->data_offset = cpu_to_le64(sb->data_offset);
	} else {
		int i;

		/* Cache device */
		sb_disk->nbuckets = cpu_to_le64(sb->nbuckets);
		sb_disk->nr_in_set = cpu_to_le16(sb->nr_in_set);
		sb_disk->nr_this_dev = cpu_to_le16(sb->nr_this_dev);
		sb_disk->bucket_size = cpu_to_le32(sb->bucket_size);

		for (i = 0; i < SB_JOURNAL_BUCKETS; i++)
			sb_disk->d[i] = cpu_to_le64(sb->d[i]);
	}

	if (sb->version >= BCACHE_SB_VERSION_CDEV_WITH_FEATURES) {
		sb_disk->feature_compat = cpu_to_le64(sb->feature_compat);
		sb_disk->feature_incompat = cpu_to_le64(sb->feature_incompat);
		sb_disk->feature_ro_compat = cpu_to_le64(sb->feature_ro_compat);
	}

	if (sb->version >= BCACHE_SB_VERSION_CDEV_WITH_FEATURES &&
	    bch_has_feature_large_bucket(sb))
		sb_disk->bucket_size = cpu_to_le16(log2_u32(sb->bucket_size));

	return sb_disk;
}

void set_bucket_size(struct cache_sb *sb, unsigned int bucket_size)
{
	if (bucket_size > USHRT_MAX) {
		sb->version = BCACHE_SB_VERSION_CDEV_WITH_FEATURES;
		bch_set_feature_large_bucket(sb);
	}

	sb->bucket_size = bucket_size;
}
