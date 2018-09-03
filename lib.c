// SPDX-License-Identifier: GPL-2.0
// Author: Shaoxiong Li <dahefanteng@gmail.com>

#include <stdbool.h>
#include <blkid.h>
#include <dirent.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include "bcache.h"
#include "lib.h"
#include <uuid.h>
#include <string.h>
#include <malloc.h>
#include <regex.h>


/*
 * utils function
 */

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
		fprintf(stderr, "Error happen when compile reg");
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
		fprintf(stderr, "Failed to open dir /sys/block/");
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
	if (dev->version == BCACHE_SB_VERSION_CDEV
	    || dev->version == BCACHE_SB_VERSION_CDEV_WITH_UUID)
		return get_cachedev_state(dev->cset, state);
	else if (dev->version == BCACHE_SB_VERSION_BDEV
		   || dev->version == BCACHE_SB_VERSION_BDEV_WITH_OFFSET)
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

	strcpy(base->name, devname);
	base->magic = "ok";
	base->first_sector = SB_SECTOR;
	base->csum = sb.csum;
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
	struct cache_sb sb;

	if (strcmp(devname, ".") == 0 || strcmp(devname, "..") == 0)
		return 0;
	char dev[261];

	sprintf(dev, "/dev/%s", devname);
	int fd = open(dev, O_RDONLY);

	if (fd == -1)
		return 0;
	if (pread(fd, &sb, sizeof(sb), SB_START) != sizeof(sb)) {
		close(fd);
		return 0;
	}
	if (memcmp(sb.magic, bcache_magic, 16)) {
		close(fd);
		return 0;
	}
	struct dev *tmp;
	int ret;

	tmp = (struct dev *) malloc(DEVLEN);
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
	struct cache_sb sb;
	uint64_t expected_csum;
	int fd = open(devname, O_RDONLY);

	if (fd < 0) {
		fprintf(stderr, "Error: Can't open dev  %s\n", devname);
		return 1;
	}

	if (pread(fd, &sb, sizeof(sb), SB_START) != sizeof(sb)) {
		fprintf(stderr, "Couldn't read\n");
		goto Fail;
	}

	if (memcmp(sb.magic, bcache_magic, 16)) {
		fprintf(stderr,
			"Bad magic,make sure this is an bcache device\n");
		goto Fail;
	}

	if (!(sb.offset == SB_SECTOR)) {
		fprintf(stderr, "Invalid superblock (bad sector)\n");
		goto Fail;
	}

	expected_csum = csum_set(&sb);
	if (!(sb.csum == expected_csum)) {
		fprintf(stderr, "Csum is not match with expected one");
		goto Fail;
	}

	*type = sb.version;
	if (sb.version == BCACHE_SB_VERSION_BDEV) {
		detail_base(devname, sb, &bd->base);
		bd->first_sector = BDEV_DATA_START_DEFAULT;
		bd->cache_mode = BDEV_CACHE_MODE(&sb);
		bd->cache_state = BDEV_STATE(&sb);
	} else if (sb.version == BCACHE_SB_VERSION_CDEV
		   || sb.version == BCACHE_SB_VERSION_CDEV_WITH_UUID) {
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
		fprintf(stderr, "Unknown bcache device type found");
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
		fprintf(stderr, "Failed to unregist this cache device");
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
		fprintf(stderr, "Failed to stop cset and its backends %m");
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
		fprintf(stderr, "Error detach device %s:%m", devname);
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
		printf("Failed to set cachemode for device %s:%m\n",
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
