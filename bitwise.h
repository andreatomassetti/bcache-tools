/* SPDX-License-Identifier: GPL-2.0 */
/**
 * Author: Coly Li
 *
 * Cherry-picked from include/uapi/linux/swab.h of
 * Linux kernel v5.3 source code.
 */

#include <linux/types.h>
#include <asm/swab.h>

#ifndef __BCH_BITWISE_H
#define __BCH_BITWISE_H

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define BCH_LITTLE_ENDIAN_LOCAL 1
#else
#undef BCH_LITTLE_ENDIAN_LOCAL
#endif


#define __swab16(x) ((__u16)(			\
	(((__u16)(x) & (__u16)0x00ffU) << 8) |	\
	(((__u16)(x) & (__u16)0xff00U) >> 8)))

#define __swab32(x) ((__u32)(				\
	(((__u32)(x) & (__u32)0x000000ffUL) << 24) |	\
	(((__u32)(x) & (__u32)0x0000ff00UL) <<  8) |	\
	(((__u32)(x) & (__u32)0x00ff0000UL) >>  8) |	\
	(((__u32)(x) & (__u32)0xff000000UL) >> 24)))

#define __swab64(x) ((__u64)(					\
	(((__u64)(x) & (__u64)0x00000000000000ffULL) << 56) |	\
	(((__u64)(x) & (__u64)0x000000000000ff00ULL) << 40) |	\
	(((__u64)(x) & (__u64)0x0000000000ff0000ULL) << 24) |	\
	(((__u64)(x) & (__u64)0x00000000ff000000ULL) <<  8) |	\
	(((__u64)(x) & (__u64)0x000000ff00000000ULL) >>  8) |	\
	(((__u64)(x) & (__u64)0x0000ff0000000000ULL) >> 24) |	\
	(((__u64)(x) & (__u64)0x00ff000000000000ULL) >> 40) |	\
	(((__u64)(x) & (__u64)0xff00000000000000ULL) >> 56)))


#ifdef BCH_LITTLE_ENDIAN_LOCAL
/* For little endian*/
#define cpu_to_le16(val)	((__le16)(val))
#define cpu_to_le32(val)	((__le32)(val))
#define cpu_to_le64(val)	((__le64)(val))

#define le16_to_cpu(val)	((__le16)(val))
#define le32_to_cpu(val)	((__le32)(val))
#define le64_to_cpu(val)	((__le64)(val))

#else
/* For big endian */
#define cpu_to_le16(val)	((__be16)__swab16((__u16)(val)))
#define cpu_to_le32(val)	((__be32)__swab32((__u32)(val)))
#define cpu_to_le64(val)	((__be64)__swab64((__u64)(val)))

#define le16_to_cpu(val)	((__be16)__swab16((__u16)(val)))
#define le32_to_cpu(val)	((__be32)__swab32((__u32)(val)))
#define le64_to_cpu(val)	((__be64)__swab64((__u64)(val)))

#endif

#endif
