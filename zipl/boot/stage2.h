/*
 * zipl - zSeries Initial Program Loader tool
 *
 * Main program for stage2 bootloader.
 *
 * Copyright IBM Corp. 2013
 * Author(s): Stefan Haberland <stefan.haberland@de.ibm.com>
 */

#ifndef COMMON_H
#define COMMON_H

#include "error.h"
#include "libc.h"
#include "cio.h"
#include "s390.h"

#define DESCR_PER_BLOCK         16

/* ADRESS */
#define STAGE2_DESC 0x78

/* Layout of ECKD disk block pointer */
struct eckd_blockptr {
	uint16_t cyl;
	uint16_t head;
	uint8_t sec;
	uint16_t size;
	uint8_t blockct;
	uint8_t reserved[8];
} __packed;

struct linear_blockptr {
	uint64_t blockno;
	uint16_t size;
	uint16_t blockct;
	uint8_t reserved[4];
} __packed;

typedef union {
	struct eckd_blockptr eckd;
	struct linear_blockptr linear;
} disk_blockptr_t;

struct component_header {
	uint8_t magic[4];
	uint8_t type;
	uint8_t reserved[27];
} __packed;

struct component_entry {
	uint8_t data[23];
	uint8_t type;
	union {
		uint32_t load_address[2];
		uint64_t load_psw;
	} address;
} __packed;

typedef enum {
	COMPONENT_EXECUTE = 0x01,
	COMPONENT_LOAD = 0x02
} component_type;

struct stage2_descr {
	uint8_t reserved[16];
} __packed __aligned(8);

void *load_direct(disk_blockptr_t *, struct subchannel_id , void *);
int extract_length(void *);
int is_zero_block(void *);
void kdump_stage2(unsigned long);


#endif /* COMMON_H */

