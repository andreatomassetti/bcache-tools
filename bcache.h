/*
 * Author: Kent Overstreet <kmo@daterainc.com>
 *
 * GPLv2
 */

#ifndef _BCACHE_H
#define _BCACHE_H

#include <linux/types.h>

#define BITMASK(name, type, field, offset, size)		\
static inline uint64_t name(const type *k)			\
{ return (k->field >> offset) & ~(((uint64_t) ~0) << size); }	\
								\
static inline void SET_##name(type *k, uint64_t v)		\
{								\
	k->field &= ~(~((uint64_t) ~0 << size) << offset);	\
	k->field |= v << offset;				\
}

static const char bcache_magic[] = {
	0xc6, 0x85, 0x73, 0xf6, 0x4e, 0x1a, 0x45, 0xca,
	0x82, 0x65, 0xf5, 0x7f, 0x48, 0xba, 0x6d, 0x81 };

/*
 * Version 0: Cache device
 * Version 1: Backing device
 * Version 2: Seed pointer into btree node checksum
 * Version 3: Cache device with new UUID format
 * Version 4: Backing device with data offset
 * Version 5: Cache adn backing devices with compat/incompat/ro_compat
 *            feature sets
 */
#define BCACHE_SB_VERSION_CDEV			0
#define BCACHE_SB_VERSION_BDEV			1
#define BCACHE_SB_VERSION_CDEV_WITH_UUID	3
#define BCACHE_SB_VERSION_BDEV_WITH_OFFSET	4
#define BCACHE_SB_VERSION_CDEV_WITH_FEATURES	5
#define BCACHE_SB_VERSION_BDEV_WITH_FEATURES	6
#define BCACHE_SB_MAX_VERSION			6

#define SB_SECTOR		8
#define SB_LABEL_SIZE		32
#define SB_JOURNAL_BUCKETS	256U
#define BDEV_DATA_START_DEFAULT	16	/* sectors */
#define SB_START		(SB_SECTOR * 512)

struct cache_sb_disk {
	__le64			csum;
	__le64			offset; /* sector where this sb was written */
	__le64			version;

	__u8			magic[16];

	__u8			uuid[16];
	union {
		__u8		set_uuid[16];
		__le64		set_magic;
	};
	__u8			label[SB_LABEL_SIZE];

	__le64			flags;
	__le64			seq;

	__le64			feature_compat;
	__le64			feature_incompat;
	__le64			feature_ro_compat;

	__le64			pad[5];

	union {
	struct {
		/* Cache devices */
		__le64		nbuckets;	/* device size */

		__le16		block_size;	/* sectors */
		__le16		bucket_size;	/* sectors */

		__le16		nr_in_set;
		__le16		nr_this_dev;
	};
	struct {
		/* Backing devices */
		__le64		data_offset;

		/*
		 * block_size from the cache device section is still used by
		 * backing devices, so don't add anything here until we fix
		 * things to not need it for backing devices anymore
		 */
	};
	};

	__le32			last_mount;		/* time overflow in y2106 */

	__le16			first_bucket;
	union {
		__le16		njournal_buckets;
		__le16		keys;
	};
	__le64			d[SB_JOURNAL_BUCKETS];	/* journal buckets */
	__le16			obso_bucket_size_hi;	/* obsoleted */
};

/*
 * This is for in-memory bcache super block.
 * NOTE: cache_sb is NOT exactly mapping to cache_sb_disk, the member
 *       size, ordering and even whole struct size may be different
 *       from cache_sb_disk.
 */
struct cache_sb {
	__u64			offset; /* sector where this sb was written */
	__u64			version;

	__u8			magic[16];

	__u8			uuid[16];
	union {
		__u8		set_uuid[16];
		__u64		set_magic;
	};
	__u8			label[SB_LABEL_SIZE];

	__u64			flags;
	__u64			seq;

	__u64			feature_compat;
	__u64			feature_incompat;
	__u64			feature_ro_compat;

	union {
	struct {
		/* Cache devices */
		__u64		nbuckets;	/* device size */

		__u16		block_size;	/* sectors */
		__u16		nr_in_set;
		__u16		nr_this_dev;
		__u32		bucket_size;	/* sectors */
	};
	struct {
		/* Backing devices */
		__u64		data_offset;

		/*
		 * block_size from the cache device section is still used by
		 * backing devices, so don't add anything here until we fix
		 * things to not need it for backing devices anymore
		 */
	};
	};

	__u32			last_mount;	/* time overflow in y2106 */

	__u16			first_bucket;
	union {
		__u16		njournal_buckets;
		__u16		keys;
	};
	__u64			d[SB_JOURNAL_BUCKETS];  /* journal buckets */
};

static inline bool SB_IS_BDEV(const struct cache_sb *sb)
{
	return sb->version == BCACHE_SB_VERSION_BDEV
		|| sb->version == BCACHE_SB_VERSION_BDEV_WITH_OFFSET;
}

BITMASK(CACHE_SYNC,		struct cache_sb, flags, 0, 1);
BITMASK(CACHE_DISCARD,		struct cache_sb, flags, 1, 1);
BITMASK(CACHE_REPLACEMENT,	struct cache_sb, flags, 2, 3);
#define CACHE_REPLACEMENT_LRU	0U
#define CACHE_REPLACEMENT_FIFO	1U
#define CACHE_REPLACEMENT_RANDOM 2U

BITMASK(BDEV_CACHE_MODE,	struct cache_sb, flags, 0, 4);
#define CACHE_MODE_WRITETHROUGH	0U
#define CACHE_MODE_WRITEBACK	1U
#define CACHE_MODE_WRITEAROUND	2U
#define CACHE_MODE_NONE		3U
BITMASK(BDEV_STATE,		struct cache_sb, flags, 61, 2);
#define BDEV_STATE_NONE		0U
#define BDEV_STATE_CLEAN	1U
#define BDEV_STATE_DIRTY	2U
#define BDEV_STATE_STALE	3U

uint64_t crc64(const void *data, size_t len);

#define node(i, j)		((void *) ((i)->d + (j)))
#define end(i)			node(i, (i)->keys)

#define csum_set(i)							\
	crc64(((void *) (i)) + 8, ((void *) end(i)) - (((void *) (i)) + 8))

/* Feature set definition */

#define BCH_FEATURE_COMPAT	0
#define BCH_FEATURE_RO_COMPAT	1
#define BCH_FEATURE_INCOMPAT	2
#define BCH_FEATURE_TYPE_MASK	0x03

#define BCH_FEATURE_COMPAT_SUPP		0
#define BCH_FEATURE_RO_COMPAT_SUPP	0
#define BCH_FEATURE_INCOMPAT_SUPP	(BCH_FEATURE_INCOMPAT_OBSO_LARGE_BUCKET| \
					 BCH_FEATURE_INCOMPAT_LOG_LARGE_BUCKET_SIZE)

#define BCH_HAS_COMPAT_FEATURE(sb, mask) \
		((sb)->feature_compat & (mask))
#define BCH_HAS_RO_COMPAT_FEATURE(sb, mask) \
		((sb)->feature_ro_compat & (mask))
#define BCH_HAS_INCOMPAT_FEATURE(sb, mask) \
		((sb)->feature_incompat & (mask))

/* Feature set definition */

/* Incompat feature set */
/* 32bit bucket size, obsoleted */
#define BCH_FEATURE_INCOMPAT_OBSO_LARGE_BUCKET		0x0001
/* real bucket size is (1 << bucket_size) */
#define BCH_FEATURE_INCOMPAT_LOG_LARGE_BUCKET_SIZE	0x0002

#define BCH_FEATURE_COMPAT_FUNCS(name, flagname) \
static inline int bch_has_feature_##name(struct cache_sb *sb) \
{ \
	return (((sb)->feature_compat & \
		BCH##_FEATURE_COMPAT_##flagname) != 0); \
} \
static inline void bch_set_feature_##name(struct cache_sb *sb) \
{ \
	(sb)->feature_compat |= \
		BCH##_FEATURE_COMPAT_##flagname; \
} \
static inline void bch_clear_feature_##name(struct cache_sb *sb) \
{ \
	(sb)->feature_compat &= \
		~BCH##_FEATURE_COMPAT_##flagname; \
}

#define BCH_FEATURE_RO_COMPAT_FUNCS(name, flagname) \
static inline int bch_has_feature_##name(struct cache_sb *sb) \
{ \
	return (((sb)->feature_ro_compat & \
		BCH##_FEATURE_RO_COMPAT_##flagname) != 0); \
} \
static inline void bch_set_feature_##name(struct cache_sb *sb) \
{ \
	(sb)->feature_ro_compat |= \
		BCH##_FEATURE_RO_COMPAT_##flagname; \
} \
static inline void bch_clear_feature_##name(struct cache_sb *sb) \
{ \
	(sb)->feature_ro_compat &= \
		~BCH##_FEATURE_RO_COMPAT_##flagname; \
}

#define BCH_FEATURE_INCOMPAT_FUNCS(name, flagname) \
static inline int bch_has_feature_##name(struct cache_sb *sb) \
{ \
	return (((sb)->feature_incompat & \
		BCH##_FEATURE_INCOMPAT_##flagname) != 0); \
} \
static inline void bch_set_feature_##name(struct cache_sb *sb) \
{ \
	(sb)->feature_incompat |= \
		BCH##_FEATURE_INCOMPAT_##flagname; \
} \
static inline void bch_clear_feature_##name(struct cache_sb *sb) \
{ \
	(sb)->feature_incompat &= \
		~BCH##_FEATURE_INCOMPAT_##flagname; \
}

BCH_FEATURE_INCOMPAT_FUNCS(obso_large_bucket, OBSO_LARGE_BUCKET);
BCH_FEATURE_INCOMPAT_FUNCS(large_bucket, LOG_LARGE_BUCKET_SIZE);

#endif
