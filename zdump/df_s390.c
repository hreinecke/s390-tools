/*
 * zgetdump - Tool for copying and converting System z dumps
 *
 * S390 dump format common functions
 *
 * Copyright IBM Corp. 2001, 2010
 * Author(s): Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "zgetdump.h"

/*
 * Check, if we can access the lowcore information in the dump
 */
static int check_addr_max(struct df_s390_hdr *hdr, u64 addr_max)
{
	unsigned int i, lc_size;

	lc_size = dfi_lc_size(df_s390_to_dfi_arch(hdr->arch));
	for (i = 0; i < hdr->cpu_cnt; i++) {
		if (hdr->lc_vec[i] + lc_size > addr_max)
			return -1;
	}
	return 0;
}

/*
 * Convert lowcore information into internal CPU representation
 */
void df_s390_cpu_info_add(struct df_s390_hdr *hdr, u64 addr_max)
{
	unsigned int i;

	if (hdr->version < 5) {
		/* No Prefix registers in header */
		hdr->cpu_cnt = 0;
		dfi_cpu_info_init(DFI_CPU_CONTENT_NONE);
	} else if (check_addr_max(hdr, addr_max) != 0) {
		/* Only lowcore pointers available */
		dfi_cpu_info_init(DFI_CPU_CONTENT_LC);
	} else {
		/* All register info available */
		dfi_cpu_info_init(DFI_CPU_CONTENT_ALL);
	}

	for (i = 0; i < hdr->cpu_cnt; i++)
		dfi_cpu_add_from_lc(hdr->lc_vec[i]);
}

/*
 * Convert s390 TOD clock into timeval structure
 */
static void tod2timeval(struct timeval *xtime, u64 todval)
{
    /* adjust todclock to 1970 */
    todval -= 0x8126d60e46000000LL - (0x3c26700LL * 1000000 * 4096);

    todval >>= 12;
    xtime->tv_sec  = todval / 1000000;
    xtime->tv_usec = todval % 1000000;
}

/*
 * Convert s390 header information into internal representation
 */
void df_s390_hdr_add(struct df_s390_hdr *hdr)
{
	struct timeval timeval;

	if (hdr->tod) {
		tod2timeval(&timeval, hdr->tod);
		dfi_attr_time_set(&timeval);
	}
	dfi_attr_version_set(hdr->version);
	dfi_arch_set(df_s390_to_dfi_arch(hdr->arch));
	if (hdr->cpu_id)
		dfi_attr_cpu_id_set(hdr->cpu_id);
	if (hdr->version >= 3 && hdr->mem_size_real)
		dfi_attr_mem_size_real_set(hdr->mem_size_real);
	if (hdr->version >= 2 && hdr->build_arch)
		dfi_attr_build_arch_set(df_s390_to_dfi_arch(hdr->build_arch));
	if (hdr->version >= 5 && hdr->real_cpu_cnt)
		dfi_attr_real_cpu_cnt_set(hdr->real_cpu_cnt);
}

/*
 * Add end marker information to internal representation
 */
void df_s390_em_add(struct df_s390_em *em)
{
	struct timeval timeval;

	if (em->tod) {
		tod2timeval(&timeval, em->tod);
		dfi_attr_time_end_set(&timeval);
	}
}

/*
 * Verify end marker
 */
int df_s390_em_verify(struct df_s390_em *em, struct df_s390_hdr *hdr)
{
	if (strncmp(em->str, DF_S390_EM_STR, strlen(DF_S390_EM_STR)) != 0)
		return -EINVAL;
	if (hdr->tod > em->tod)
		return -EINVAL;
	return 0;
}

/*
 * Read s390 dump tool from DASD with given block size
 */
void df_s390_dumper_read(struct zg_fh *fh, int blk_size,
			 struct df_s390_dumper *dumper)
{
	int offset = DF_S390_MAGIC_BLK_ECKD * blk_size;

	zg_seek(fh, offset, ZG_CHECK);
	zg_read(fh, dumper, sizeof(*dumper), ZG_CHECK);
}
