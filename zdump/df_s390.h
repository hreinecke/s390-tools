/*
 * zgetdump - Tool for copying and converting System z dumps
 *
 * S390 dump format common functions
 *
 * Copyright IBM Corp. 2001, 2010
 * Author(s): Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#ifndef DF_S390_H
#define DF_S390_H

#include "dt.h"
#include "zg.h"

#define DF_S390_MAGIC		0xa8190173618f23fdULL
#define DF_S390_HDR_SIZE	0x1000
#define DF_S390_EM_SIZE		16
#define DF_S390_EM_STR		"DUMP_END"
#define DF_S390_CPU_MAX		512
#define DF_S390_MAGIC_BLK_ECKD	3

/*
 * Architecture of dumped system
 */
enum df_s390_arch {
	DF_S390_ARCH_32	= 1,
	DF_S390_ARCH_64	= 2,
};

/*
 * s390 dump header format
 */
struct df_s390_hdr {
	u64	magic;				/* 0x000 */
	u32	version;			/* 0x008 */
	u32	hdr_size;			/* 0x00c */
	u32	dump_level;			/* 0x010 */
	u32	page_size;			/* 0x014 */
	u64	mem_size;			/* 0x018 */
	u64	mem_start;			/* 0x020 */
	u64	mem_end;			/* 0x028 */
	u32	num_pages;			/* 0x030 */
	u32	pad;				/* 0x034 */
	u64	tod;				/* 0x038 */
	u64	cpu_id;				/* 0x040 */
	u32	arch;				/* 0x048 */
	u32	volnr;				/* 0x04c */
	u32	build_arch;			/* 0x050 */
	u64	mem_size_real;			/* 0x054 */
	u8	mvdump;				/* 0x05c */
	u16	cpu_cnt;			/* 0x05d */
	u16	real_cpu_cnt;			/* 0x05f */
	u8	end_pad1[0x200-0x061];		/* 0x061 */
	u64	mvdump_sign;			/* 0x200 */
	u64	mvdump_zipl_time;		/* 0x208 */
	u8	end_pad2[0x800-0x210];		/* 0x210 */
	u32	lc_vec[DF_S390_CPU_MAX];	/* 0x800 */
} __attribute__((packed));

/*
 *  End marker: Should be at the end of every valid s390 crash dump.
 */
struct df_s390_em {
	char	str[8];
	u64	tod;
} __attribute__((packed));

/*
 * Convert DFI arch to s390 arch
 */
static inline enum df_s390_arch df_s390_from_dfi_arch(enum dfi_arch dfi_arch)
{
	return dfi_arch == DFI_ARCH_64 ? DF_S390_ARCH_64 : DF_S390_ARCH_32;
}

/*
 * Convert s390 arch to DFI arch
 */
static inline enum dfi_arch df_s390_to_dfi_arch(enum df_s390_arch df_s390_arch)
{
	return df_s390_arch == DF_S390_ARCH_64 ? DFI_ARCH_64 : DFI_ARCH_32;
}

/*
 * Dump tool structure (version 1)
 */
struct df_s390_dumper_v1 {
	char	code[0xff7 - 0x8];
	u8	force;
	u64	mem;
} __attribute__ ((packed));

#define DF_S390_DUMPER_SIZE_V1	0x1000

/*
 * Dump tool structure (version 2)
 */
struct df_s390_dumper_v2 {
	char	code[0x1ff7 - 0x8];
	u8	force;
	u64	mem;
} __attribute__ ((packed));

#define DF_S390_DUMPER_SIZE_V2	0x2000

/*
 * Dump tool structure
 */
struct df_s390_dumper {
	char		magic[7];
	u8		version;
	union {
		struct df_s390_dumper_v1	v1;
		struct df_s390_dumper_v2	v2;
	} d;
} __attribute__ ((packed));

/*
 * Dumper member access helpers
 */
#define df_s390_dumper_magic(dumper) ((dumper).magic)
#define df_s390_dumper_version(dumper) ((dumper).version)
#define df_s390_dumper_mem(dumper) \
	((dumper).version == 1 ? dumper.d.v1.mem : dumper.d.v2.mem)
#define df_s390_dumper_force(dumper) \
	((dumper).version == 1 ? dumper.d.v1.force : dumper.d.v2.force)
#define df_s390_dumper_size(dumper) \
	((dumper).version == 1 ? 0x1000 : 0x2000)

/*
 * s390 dump helpers
 */
extern void df_s390_hdr_add(struct df_s390_hdr *hdr);
extern void df_s390_em_add(struct df_s390_em *em);
extern void df_s390_cpu_info_add(struct df_s390_hdr *hdr, u64 addr_max);
extern int df_s390_em_verify(struct df_s390_em *em, struct df_s390_hdr *hdr);
extern void df_s390_dumper_read(struct zg_fh *fh, int32_t blk_size,
				struct df_s390_dumper *dumper);

/*
 * DASD multi-volume dumper functions
 */
extern int dt_s390mv_init(void);
extern void dt_s390mv_exit(void);
extern void dt_s390mv_info(void);

#endif /* DF_S390_H */
