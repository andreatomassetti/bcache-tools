// SPDX-License-Identifier: GPL-2.0
// Author: Shaoxiong Li <dahefanteng@gmail.com>

#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <getopt.h>
#include <regex.h>
#include "bcache.h"
#include "lib.h"
#include "make.h"
#include "libsmartcols.h"
#include <locale.h>
#include "list.h"


//utils function
static bool accepted_char(char c)
{
	if ('0' <= c && c <= '9')
		return true;
	if ('A' <= c && c <= 'Z')
		return true;
	if ('a' <= c && c <= 'z')
		return true;
	if (strchr(".-_", c))
		return true;
	return false;
}

static void print_encode(char *in)
{
	char *pos;

	for (pos = in; *pos; pos++)
		if (accepted_char(*pos))
			putchar(*pos);
		else
			printf("%%%x", *pos);
}

bool bad_uuid(char *uuid)
{
	const char *pattern =
	    "^[a-z0-9]{8}-[a-z0-9]{4}-[a-z0-9]{4}-[a-z0-9]{4}-[a-z0-9]{12}$";
	regex_t reg;
	int status;
	regmatch_t regmatche;

	if (regcomp(&reg, pattern, REG_EXTENDED) != 0)
		fprintf(stderr, "Error happen when check uuid format:%m");
	status = regexec(&reg, uuid, 1, &regmatche, 0);
	regfree(&reg);
	if (status == REG_NOMATCH)
		return true;
	else
		return false;
}

bool bad_dev(char *devname)
{
	char *pattern = "^/dev/[a-zA-Z0-9-]*$";
	regex_t reg;
	int status;
	regmatch_t regmatche;

	if (regcomp(&reg, pattern, REG_EXTENDED) != 0) {
		fprintf(stderr,
			"Error happen when check device name format:%m");
	}
	status = regexec(&reg, devname, 1, &regmatche, 0);
	regfree(&reg);
	if (status == REG_NOMATCH)
		return true;
	else
		return false;
}


int main_usage(void)
{
	fprintf(stderr,
		"Usage:bcache [SUBCMD]\n"
		"	show		show all bcache devices in this host\n"
		"	tree		show active bcache devices in this host\n"
		"	make		make regular device to bcache device\n"
		"	register	register device to kernel\n"
		"	unregister	unregister device from kernel\n"
		"	attach		attach backend device(data device) to cache device\n"
		"	detach		detach backend device(data device) from cache device\n"
		"	set-cachemode	set cachemode for backend device\n");
	return EXIT_FAILURE;
}

int show_usage(void)
{
	fprintf(stderr,
		"Usage:	show [option]\n"
		"	show overall information about all devices\n"
		"	-d	--device {devname}	show the detail infomation about this device\n"
		"	-m	--more			show overall information about all devices with detail info\n"
		"	-h	--help			show help information\n");
	return EXIT_FAILURE;
}

int tree_usage(void)
{
	fprintf(stderr,
		"Usage: tree	show active bcache devices in this host\n");
	return EXIT_FAILURE;
}

int register_usage(void)
{
	fprintf(stderr,
		"Usage:register devicename		register device as bcache device to kernel\n");
	return EXIT_FAILURE;
}

int unregister_usage(void)
{
	fprintf(stderr,
		"Usage:unregister devicename		unregister device from kernel\n");
	return EXIT_FAILURE;
}

int attach_usage(void)
{
	fprintf(stderr, "Usage:attach cset_uuid|cachedevice datadevice\n");
	return EXIT_FAILURE;
}

int detach_usage(void)
{
	fprintf(stderr, "Usage:detach devicename\n");
	return EXIT_FAILURE;
}

int setcachemode_usage(void)
{
	fprintf(stderr, "Usage:set-cachemode devicename modetype\n");
	return EXIT_FAILURE;
}


void free_dev(struct list_head *head)
{
	struct dev *dev;

	list_for_each_entry(dev, head, dev_list) {
		free(dev);
	}
}

int show_bdevs_detail(void)
{
	struct list_head head;
	struct dev *devs;

	INIT_LIST_HEAD(&head);
	int ret;

	ret = list_bdevs(&head);
	if (ret != 0) {
		fprintf(stderr, "Failed to list devices\n");
		return ret;
	}
	printf("Name\t\tUuid\t\t\t\t\tCset_Uuid\t\t\t\tType\t\tState");
	printf("\t\t\tBname\t\tAttachToDev\tAttachToCset\n");
	list_for_each_entry(devs, &head, dev_list) {
		printf("%s\t%s\t%s\t%lu", devs->name, devs->uuid,
		       devs->cset, devs->version);
		switch (devs->version) {
			// These are handled the same by the kernel
		case BCACHE_SB_VERSION_CDEV:
		case BCACHE_SB_VERSION_CDEV_WITH_UUID:
			printf(" (cache)");
			break;
			// The second adds data offset supporet
		case BCACHE_SB_VERSION_BDEV:
		case BCACHE_SB_VERSION_BDEV_WITH_OFFSET:
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
	struct dev *devs;

	INIT_LIST_HEAD(&head);
	int ret;

	ret = list_bdevs(&head);
	if (ret != 0) {
		fprintf(stderr, "Failed to list devices\n");
		return ret;
	}

	printf("Name\t\tType\t\tState\t\t\tBname\t\tAttachToDev\n");
	list_for_each_entry(devs, &head, dev_list) {
		printf("%s\t%lu", devs->name, devs->version);
		switch (devs->version) {
			// These are handled the same by the kernel
		case BCACHE_SB_VERSION_CDEV:
		case BCACHE_SB_VERSION_CDEV_WITH_UUID:
			printf(" (cache)");
			break;

			// The second adds data offset supporet
		case BCACHE_SB_VERSION_BDEV:
		case BCACHE_SB_VERSION_BDEV_WITH_OFFSET:
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
	if (type == BCACHE_SB_VERSION_BDEV) {
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
	} else if (type == BCACHE_SB_VERSION_CDEV
		   || type == BCACHE_SB_VERSION_CDEV_WITH_UUID) {
		printf("sb.magic\t\t%s\n", cd.base.magic);
		printf("sb.first_sector\t\t%" PRIu64 "\n",
		       cd.base.first_sector);
		printf("sb.csum\t\t\t%" PRIX64 "\n", cd.base.csum);
		printf("sb.version\t\t%" PRIu64, cd.base.version);
		printf(" [cache device]\n");
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

int tree(void)
{
	struct list_head head;
	struct dev *devs, *tmp;

	INIT_LIST_HEAD(&head);
	int ret;

	ret = list_bdevs(&head);
	if (ret != 0) {
		fprintf(stderr, "Failed to list devices\n");
		return ret;
	}
	struct libscols_table *tb;
	struct libscols_line *dad, *son;
	enum { COL_CSET, COL_BNAME };
	setlocale(LC_ALL, "");
	tb = scols_new_table();
	scols_table_new_column(tb, ".", 0.1, SCOLS_FL_TREE);
	scols_table_new_column(tb, "", 2, SCOLS_FL_TRUNC);
	list_for_each_entry(devs, &head, dev_list) {
		if ((devs->version == BCACHE_SB_VERSION_CDEV
		     || devs->version == BCACHE_SB_VERSION_CDEV_WITH_UUID)
		    && strcmp(devs->state, BCACHE_BASIC_STATE_ACTIVE) == 0) {
			dad = scols_table_new_line(tb, NULL);
			scols_line_set_data(dad, COL_CSET, devs->name);
			list_for_each_entry(tmp, &head, dev_list) {
				if (strcmp(devs->cset, tmp->attachuuid) ==
				    0) {
					son =
					    scols_table_new_line(tb, dad);
					scols_line_set_data(son, COL_CSET,
							    tmp->name);
					scols_line_set_data(son, COL_BNAME,
							    tmp->bname);
				}
			}
		}
	}
	scols_print_table(tb);
	scols_unref_table(tb);
	free_dev(&head);
	return 0;
}

int attach_both(char *cdev, char *backdev)
{
	struct bdev bd;
	struct cdev cd;
	int type = 1;
	int ret;
	char buf[100];

	ret = detail_dev(backdev, &bd, &cd, &type);
	if (ret < 0)
		return ret;
	if (type != BCACHE_SB_VERSION_BDEV
	    && type != BCACHE_SB_VERSION_BDEV_WITH_OFFSET) {
		fprintf(stderr, "%s is not an backend device\n", backdev);
		return 1;
	}
	if (strcmp(bd.base.attachuuid, BCACHE_BNAME_NOT_EXIST) != 0) {
		fprintf(stderr,
			"This device have attached to another cset\n");
		return 1;
	}

	if (strlen(cdev) != 36) {
		ret = detail_dev(cdev, &bd, &cd, &type);
		if (type != BCACHE_SB_VERSION_CDEV
		    && type != BCACHE_SB_VERSION_CDEV_WITH_UUID) {
			fprintf(stderr, "%s is not an cache device", cdev);
			return 1;
		}
		strcpy(buf, cd.base.cset);
	} else {
		strcpy(buf, cdev);
	}
	return attach_backdev(buf, backdev);
}

int main(int argc, char **argv)
{
	char *subcmd;

	if (argc < 2) {
		main_usage();
		return 1;
	}
	subcmd = argv[1];
	argc--;
	argv += 1;
	if (strcmp(subcmd, "make") == 0)
		return make_bcache(argc, argv);
	else if (strcmp(subcmd, "show") == 0) {
		int o = 0;
		char devname[40];
		int more = 0;
		int device = 0;
		int help = 0;

		static struct option long_options[] = {
			{"more", no_argument, 0, 'm'},
			{"help", no_argument, 0, 'h'},
			{"device", required_argument, 0, 'd'},
			{0, 0, 0, 0}
		};
		int option_index = 0;

		while ((o =
			getopt_long(argc, argv, "hmd:", long_options,
				    &option_index)) != EOF) {
			switch (o) {
			case 'd':
				strcpy(devname, optarg);
				device = 1;
				break;
			case 'm':
				more = 1;
				break;
			case 'h':
				help = 1;
				break;
			case '?':
				return 1;
			}
		}
		argc -= optind;
		if (help || argc != 0) {
			return show_usage();
		} else if (more) {
			return show_bdevs_detail();
		} else if (device) {
			if (bad_dev(devname)) {
				fprintf(stderr,
					"Error:Wrong device name found\n");
				return 1;
			}
			return detail_single(devname);
		} else {
			return show_bdevs();
		}
	} else if (strcmp(subcmd, "tree") == 0) {
		if (argc != 1)
			return tree_usage();
		return tree();
	} else if (strcmp(subcmd, "register") == 0) {
		if (argc != 2 || strcmp(argv[1], "-h") == 0)
			return register_usage();
		if (bad_dev(argv[1])) {
			fprintf(stderr, "Error:Wrong device name found\n");
			return 1;
		}
		return register_dev(argv[1]);
	} else if (strcmp(subcmd, "unregister") == 0) {
		if (argc != 2 || strcmp(argv[1], "-h") == 0)
			return unregister_usage();
		if (bad_dev(argv[1])) {
			fprintf(stderr, "Error:Wrong device name found\n");
			return 1;
		}
		struct bdev bd;
		struct cdev cd;
		int type = 1;
		int ret;

		ret = detail_dev(argv[1], &bd, &cd, &type);
		if (ret != 0)
			return ret;
		if (type == BCACHE_SB_VERSION_BDEV) {
			return stop_backdev(argv[1]);
		} else if (type == BCACHE_SB_VERSION_CDEV
			   || type == BCACHE_SB_VERSION_CDEV_WITH_UUID) {
			return unregister_cset(cd.base.cset);
		}
		return 1;
	} else if (strcmp(subcmd, "attach") == 0) {
		if (argc != 3 || strcmp(argv[1], "-h") == 0)
			return attach_usage();
		if ((bad_dev(argv[1]) && bad_uuid(argv[1]))
		    || bad_dev(argv[2])) {
			fprintf(stderr,
				"Error:Wrong device name or cache_set uuid found\n");
			return 1;
		}
		return attach_both(argv[1], argv[2]);
	} else if (strcmp(subcmd, "detach") == 0) {
		if (argc != 2 || strcmp(argv[1], "-h") == 0)
			return detach_usage();
		if (bad_dev(argv[1])) {
			fprintf(stderr, "Error:Wrong device name found\n");
			return 1;
		}
		return detach_backdev(argv[1]);
	} else if (strcmp(subcmd, "set-cachemode") == 0) {
		if (argc != 3)
			return setcachemode_usage();
		if (bad_dev(argv[1])) {
			fprintf(stderr, "Error:Wrong device name found\n");
			return 1;
		}
		return set_backdev_cachemode(argv[1], argv[2]);
	}
	main_usage();
	return 0;
}
