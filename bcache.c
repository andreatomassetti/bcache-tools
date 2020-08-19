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
#include <locale.h>
#include "list.h"
#include <limits.h>
#include <assert.h>

#include "features.h"

#define BCACHE_TOOLS_VERSION	"1.1"

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
		fprintf(stderr, "Error happen when check uuid format:%m\n");
	status = regexec(&reg, uuid, 1, &regmatche, 0);
	regfree(&reg);
	if (status == REG_NOMATCH)
		return true;
	else
		return false;
}

bool bad_dev(char **devname)
{

	char *ptr = realpath(*devname, NULL);

	if (ptr == NULL) {
		fprintf(stderr, "Error:Failed to resolve device name\n");
		return true;
	}
	*devname = ptr;
	char *pattern = "^/dev/[a-zA-Z0-9-]*$";
	regex_t reg;
	int status;
	regmatch_t regmatche;

	if (regcomp(&reg, pattern, REG_EXTENDED) != 0) {
		fprintf(stderr,
			"Error happen when check device name format:%m\n");
	}
	status = regexec(&reg, *devname, 1, &regmatche, 0);
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
		"	set-cachemode	set cachemode for backend device\n"
		"	set-label	set label for backend device\n");
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

int setlabel_usage(void)
{
	fprintf(stderr,
		"Usage:set-label devicename label\n(only for backend device)\n");
	return EXIT_FAILURE;
}

int version_usagee(void)
{
	fprintf(stderr,
		"Usage: version		display software version\n");
	return EXIT_FAILURE;
}

void free_dev(struct list_head *head)
{
	struct dev *dev, *n;

	list_for_each_entry_safe(dev, n, head, dev_list) {
		free(dev);
	}
}

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
	printf("Name\t\tUuid\t\t\t\t\tCset_Uuid\t\t\t\tType\t\tState");
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

void replace_line(char **dest, const char *from, const char *to)
{
	assert(strlen(from) == strlen(to));
	char sub[4096] = "";
	char new[4096] = "";

	strcpy(sub, *dest);
	while (1) {
		char *tmp = strpbrk(sub, from);

		if (tmp != NULL) {
			strcpy(new, tmp);
			strcpy(sub, tmp + strlen(from));
		} else
			break;
	}
	if (strlen(new) > 0) {
		strncpy(new, to, strlen(to));
		sprintf(*dest + strlen(*dest) - strlen(new), new, strlen(new));
	}
}

int tree(void)
{
	char *out = (char *)malloc(4096);
	const char *begin = ".\n";
	const char *middle = "├─";
	const char *tail = "└─";
	struct list_head head;
	struct dev *devs, *tmp, *n, *m;

	INIT_LIST_HEAD(&head);
	int ret;

	ret = list_bdevs(&head);
	if (ret != 0) {
		fprintf(stderr, "Failed to list devices\n");
		return ret;
	}
	sprintf(out, "%s", begin);
	list_for_each_entry_safe(devs, n, &head, dev_list) {
		if ((devs->version == BCACHE_SB_VERSION_CDEV
		     || devs->version == BCACHE_SB_VERSION_CDEV_WITH_UUID)
		    && strcmp(devs->state, BCACHE_BASIC_STATE_ACTIVE) == 0) {
			sprintf(out + strlen(out), "%s\n", devs->name);
			list_for_each_entry_safe(tmp, m, &head, dev_list) {
				if (strcmp(devs->cset, tmp->attachuuid) ==
				    0) {
					replace_line(&out, tail, middle);
					sprintf(out + strlen(out), "%s%s %s\n",
						tail, tmp->name, tmp->bname);
				}
			}
		}
	}
	if (strlen(out) > strlen(begin))
		printf("%s", out);
	free_dev(&head);
	free(out);
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
	if (ret != 0)
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
			fprintf(stderr, "%s is not an cache device\n", cdev);
			return 1;
		}
		strcpy(buf, cd.base.cset);
	} else {
		strcpy(buf, cdev);
	}
	return attach_backdev(buf, backdev);
}

bool has_permission(void)
{
	uid_t euid = geteuid();

	if (euid != 0)
		return false;
	return true;
}

int main(int argc, char **argv)
{
	char *subcmd;

	if (!has_permission()) {
		fprintf(stderr,
		"Only root or users who has root priviledges can run this command\n");
		return 1;
	}
	if (argc < 2) {
		main_usage();
		return 1;
	}
	subcmd = argv[1];
	argc--;
	argv += 1;
	char *devname = NULL;
	if (strcmp(subcmd, "make") == 0)
		return make_bcache(argc, argv);
	else if (strcmp(subcmd, "show") == 0) {
		int o = 0;
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
				devname = optarg;
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
			if (bad_dev(&devname)) {
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
		devname = argv[1];
		if (bad_dev(&devname)) {
			fprintf(stderr, "Error:Wrong device name found\n");
			return 1;
		}
		return register_dev(devname);
	} else if (strcmp(subcmd, "unregister") == 0) {
		if (argc != 2 || strcmp(argv[1], "-h") == 0)
			return unregister_usage();
		devname = argv[1];
		if (bad_dev(&devname)) {
			fprintf(stderr, "Error:Wrong device name found\n");
			return 1;
		}
		struct bdev bd;
		struct cdev cd;
		int type = 1;
		int ret;

		ret = detail_dev(devname, &bd, &cd, &type);
		if (ret != 0)
			return ret;
		if (type == BCACHE_SB_VERSION_BDEV) {
			return stop_backdev(devname);
		} else if (type == BCACHE_SB_VERSION_CDEV
			   || type == BCACHE_SB_VERSION_CDEV_WITH_UUID) {
			return unregister_cset(cd.base.cset);
		}
		return 1;
	} else if (strcmp(subcmd, "attach") == 0) {
		if (argc != 3 || strcmp(argv[1], "-h") == 0)
			return attach_usage();
		devname = argv[2];
		char *attachto = argv[1];

		if ((bad_uuid(attachto) && bad_dev(&attachto))
			|| bad_dev(&devname)) {
			fprintf(stderr,
			"Error:Wrong device name or cache_set uuid found\n");
			return 1;
		}
		return attach_both(attachto, devname);
	} else if (strcmp(subcmd, "detach") == 0) {
		if (argc != 2 || strcmp(argv[1], "-h") == 0)
			return detach_usage();
		devname = argv[1];
		if (bad_dev(&devname)) {
			fprintf(stderr, "Error:Wrong device name found\n");
			return 1;
		}
		return detach_backdev(devname);
	} else if (strcmp(subcmd, "set-cachemode") == 0) {
		if (argc != 3)
			return setcachemode_usage();
		devname = argv[1];
		if (bad_dev(&devname)) {
			fprintf(stderr, "Error:Wrong device name found\n");
			return 1;
		}
		struct bdev bd;
		struct cdev cd;
		int type = 1;
		int ret;

		ret = detail_dev(devname, &bd, &cd, &type);
		if (ret != 0) {
			fprintf(stderr,
			"This device doesn't exist or failed to receive info from this device\n");
			return ret;
		}
		if (type != BCACHE_SB_VERSION_BDEV
		    && type != BCACHE_SB_VERSION_BDEV_WITH_OFFSET) {
			fprintf(stderr,
				"Only backend device is suppported\n");
			return 1;
		}
		return set_backdev_cachemode(devname, argv[2]);
	} else if (strcmp(subcmd, "set-label") == 0) {
		if (argc != 3)
			return setlabel_usage();
		devname = argv[1];
		if (bad_dev(&devname)) {
			fprintf(stderr, "Error:Wrong device name found\n");
			return 1;
		}
		struct bdev bd;
		struct cdev cd;
		int type = 5;
		int ret;

		ret = detail_dev(devname, &bd, &cd, &type);
		if (ret != 0) {
			fprintf(stderr,
		"This device doesn't exist or failed to receive info from this device\n");
			return ret;
		}
		if (type != BCACHE_SB_VERSION_BDEV
		    && type != BCACHE_SB_VERSION_BDEV_WITH_OFFSET) {
			fprintf(stderr,
				"Only backend device is suppported\n");
			return 1;
		}
		if (strlen(argv[2]) >= SB_LABEL_SIZE) {
			fprintf(stderr, "Label is too long\n");
			return 1;
		}
		return set_label(devname, argv[2]);
	} else if (strcmp(subcmd, "version") == 0) {
		if (argc != 1)
			return version_usagee();
		printf("bcache-tools %s\n", BCACHE_TOOLS_VERSION);

		return 0;
	}

	main_usage();
	return 0;
}
