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
#include "show.h"

#define BCACHE_TOOLS_VERSION	"1.1"

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
	char *out;
	const char *begin = ".\n";
	const char *middle = "├─";
	const char *tail = "└─";
	struct list_head head;
	struct dev *devs, *tmp, *n, *m;

	INIT_LIST_HEAD(&head);
	int ret;

	out = (char *)malloc(4096);
	if (out == NULL) {
		fprintf(stderr, "Error: fail to allocate memory buffer\n");
		return 1;
	}

	ret = list_bdevs(&head);
	if (ret != 0) {
		free(out);
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
