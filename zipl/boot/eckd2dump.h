/*
 * zipl - zSeries Initial Program Loader tool
 *
 * Common ECKD dump I/O functions
 *
 * Copyright IBM Corp. 2013
 * Author(s): Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */
#ifndef ECKD2DUMP_H
#define ECKD2DUMP_H

#include "cio.h"

#define ECKD_BPWRT_MAX 64

struct eckd_device {
	uint32_t blk_start;
	uint32_t blk_end;
	uint16_t blk_size;
	uint8_t num_heads;
	uint8_t bpt;
	struct subchannel_id ssid;
	uint16_t devno;
};

extern struct eckd_device device;

/*
 * Convert memory size to number of blocks
 */
static inline unsigned long m2b(unsigned long mem)
{
	return mem / device.blk_size;
}

/*
 * Convert number of blocks to memory size
 */
static inline unsigned long b2m(unsigned long blk)
{
	return blk * device.blk_size;
}

void stage2dump_eckd_init();
void writeblock(unsigned long blk, unsigned long addr, unsigned long blk_count,
		unsigned long zero_page);
void readblock(unsigned long blk, unsigned long addr, unsigned long blk_count);

#endif /* ECKD2DUMP_H */
