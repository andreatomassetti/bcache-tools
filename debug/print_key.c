#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <limits.h>

#include "../bcache.h"

struct bkey {
	__u64   high;
	__u64   low;
	__u64   ptr;
};

#define KEY_SIZE_BITS	   16
#define KEY_MAX_U64S	    8


#define KEY_FIELD(name, field, offset, size)		\
	BITMASK(name, struct bkey, field, offset, size)

#define PTR_FIELD(name, offset, size)			\
static inline __u64 name(const struct bkey *k)		\
{ return (k->ptr >> offset) & ~(~0ULL << size); }

KEY_FIELD(KEY_PTRS,     high, 60, 3)
KEY_FIELD(HEADER_SIZE,  high, 58, 2)
KEY_FIELD(KEY_CSUM,     high, 56, 2)
KEY_FIELD(KEY_PINNED,   high, 55, 1)
KEY_FIELD(KEY_DIRTY,    high, 36, 1)
KEY_FIELD(KEY_SIZE,     high, 20, KEY_SIZE_BITS)
KEY_FIELD(KEY_INODE,    high, 0,  20)

#define PTR_DEV_BITS		12

PTR_FIELD(PTR_DEV,	51, PTR_DEV_BITS)
PTR_FIELD(PTR_OFFSET,	8,  43)
PTR_FIELD(PTR_GEN,	0,  8)

void usage()
{
	printf("print_key <high> <low> <ptr>\n");
	exit(1);
}

int main(int argc, char *argv[])
{
	struct bkey k;

	if (argc != 4)
		usage();

	k.high = strtoul(argv[1], NULL, 0);
	if (k.high == ULLONG_MAX) {
		printf("invalid key high %llu (0x%llx)\n",
		       k.high, k.high);
		exit(1);
	}

	k.low = strtoul(argv[2], NULL, 0);
	if (k.high == ULLONG_MAX) {
		printf("invalid key low %llu (0x%llx)\n",
		       k.low, k.low);
		exit(1);
	}

	k.ptr = strtoul(argv[3], NULL, 0);
	if (k.high == ULLONG_MAX) {
		printf("invalid key ptr %llu (0x%llx)\n",
		       k.ptr, k.ptr);
		exit(1);
	}

	printf("key {h: %llu, l: %llu, p: %llu} / {0x%llx, 0x%llx, 0x%llx}\n",
	       k.high, k.low, k.ptr, k.high, k.low, k.ptr);

	printf("KEY_INODE	%lu (0x%lx)\n", KEY_INODE(&k), KEY_INODE(&k));
	printf("KEY_SIZE	%lu (0x%lx)\n", KEY_SIZE(&k), KEY_SIZE(&k));
	printf("KEY_DIRTY	%lu (0x%lx)\n", KEY_DIRTY(&k), KEY_DIRTY(&k));
	printf("KEY_PINNED	%lu (0x%lx)\n", KEY_PINNED(&k), KEY_PINNED(&k));
	printf("KEY_CSUM	%lu (0x%lx)\n", KEY_CSUM(&k), KEY_CSUM(&k));
	printf("HEADER_SIZE	%lu (0x%lx)\n", HEADER_SIZE(&k), HEADER_SIZE(&k));
	printf("KEY_PTRS	%lu (0x%lx)\n", KEY_PTRS(&k), KEY_PTRS(&k));
	printf("PTR_GEN		%llu (0x%llx)\n", PTR_GEN(&k), PTR_GEN(&k));
	printf("PTR_OFFSET	%llu (0x%llx)\n", PTR_OFFSET(&k), PTR_OFFSET(&k));
	printf("PTR_DEV		%llu (0x%llx)\n", PTR_DEV(&k), PTR_DEV(&k));
	return 0;
}
