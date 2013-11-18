/*
 * zgetdump - Tool for copying and converting System z dumps
 *
 * ELF core dump format definitions
 *
 * Copyright IBM Corp. 2001, 2010
 * Author(s): Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#ifndef DF_ELF_H
#define DF_ELF_H

#include <linux/types.h>
#include <elf.h>
#include "zg.h"
#include "dfo.h"

/*
 * S390 CPU timer note (u64)
 */
#ifndef NT_S390_TIMER
#define NT_S390_TIMER 0x301
#endif

/*
 * S390 TOD clock comparator note (u64)
 */
#ifndef NT_S390_TODCMP
#define NT_S390_TODCMP 0x302
#endif

/*
 * S390 TOD programmable register note (u32)
 */
#ifndef NT_S390_TODPREG
#define NT_S390_TODPREG 0x303
#endif

/*
 * S390 control registers note (16 * u32)
 */
#ifndef NT_S390_CTRS
#define NT_S390_CTRS 0x304
#endif

/*
 * S390 prefix note (u32)
 */
#ifndef NT_S390_PREFIX
#define NT_S390_PREFIX 0x305
#endif

/*
 * prstatus ELF Note
 */
struct nt_prstatus_64 {
	u8	pad1[32];
	u32	pr_pid;
	u8	pad2[76];
	u64	psw[2];
	u64	gprs[16];
	u32	acrs[16];
	u64	orig_gpr2;
	u32	pr_fpvalid;
	u8	pad3[4];
} __attribute__ ((packed));

/*
 * fpregset ELF Note
 */
struct nt_fpregset_64 {
	u32	fpc;
	u32	pad;
	u64	fprs[16];
} __attribute__ ((packed));

/*
 * prpsinfo ELF Note
 */
struct nt_prpsinfo_64 {
	char	pr_state;
	char	pr_sname;
	char	pr_zomb;
	char	pr_nice;
	u64	pr_flag;
	u32	pr_uid;
	u32	pr_gid;
	u32	pr_pid, pr_ppid, pr_pgrp, pr_sid;
	char	pr_fname[16];
	char	pr_psargs[80];
};

static inline void df_elf_ensure_s390x(void)
{
#ifndef __s390x__
	ERR_EXIT("The ELF dump format is only supported on s390x (64 bit)");
#endif
}

#endif /* DF_ELF_H */
