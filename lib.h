/*
 * Author: Shaoxiong Li <dahefanteng@gmail.com>
 *
 * GPLv2
 */


#include "list.h"

struct dev {
	char name[40];
	char *magic;
	uint64_t first_sector;
	uint64_t csum;
	int version;
	char label[SB_LABEL_SIZE + 1];
	char uuid[40];
	int sectors_per_block;
	int sectors_per_bucket;
	char cset[40];
	char state[40];
	char bname[40];
	char attachuuid[40];
	struct list_head dev_list;
};

struct bdev {
	struct dev base;
	int first_sector;
	int cache_mode;
	int cache_state;
};

//typedef int bool;
struct cdev {
	struct dev base;
	int first_sector;
	int cache_sectors;
	int total_sectors;
	bool ordered;
	bool discard;
	int pos;
	unsigned int replacement;
};


int list_bdevs(struct list_head *head);
int detail_dev(char *devname, struct bdev *bd, struct cdev *cd, int *type);
int regist(char *devname);
int stop_backdev(char *devname);
int unregist_cset(char *cset);
int attach(char *cset, char *devname);
int detach(char *devname);
int set_backdev_cachemode(char *devname, char *cachemode);
int cset_to_devname(struct list_head *head, char *cset, char *devname);


#define DEVLEN sizeof(struct dev)

#define BCACHE_NO_SUPPORT		"N/A"

#define BCACHE_BASIC_STATE_ACTIVE	"active"
#define BCACHE_BASIC_STATE_INACTIVE	"inactive"

#define BCACHE_ATTACH_ALONE		"Alone"
#define BCACHE_BNAME_NOT_EXIST		"Non-Exist"
#define DEV_PREFIX_LEN			5
