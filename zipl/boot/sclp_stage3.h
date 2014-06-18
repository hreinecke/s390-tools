/*
 * zipl - zSeries Initial Program Loader tool
 *
 * Functions for console input and output.
 *
 * Copyright IBM Corp. 2013
 * Author(s): Stefan Haberland <stefan.haberland@de.ibm.com>
 */

#ifndef SCLP_STAGE3_H
#define SCLP_STAGE3_H

#include "sclp.h"

#define SDIAS_EVSTATE_ALL_STORED  0x00
#define SDIAS_EVSTATE_PART_STORED 0x10

#define SDIAS_EVSTATE_ALL_STORED  0x00
#define SDIAS_EVSTATE_PART_STORED 0x10

struct sdias_evbuf {
	struct  evbuf_header header;
	uint8_t      event_qual;
	uint8_t      data_id;
	uint64_t     reserved2;
	uint32_t     event_id;
	uint16_t     reserved3;
	uint8_t      asa_size;
	uint8_t      event_status;
	uint32_t     reserved4;
	uint32_t     blk_cnt;
	uint64_t     asa;
	uint32_t     reserved5;
	uint32_t     fbn;
	uint32_t     reserved6;
	uint32_t     lbn;
	uint16_t     reserved7;
	uint16_t     dbs;
} __packed;

struct sdias_sccb {
	struct sccb_header  header;
	struct sdias_evbuf  evbuf;
} __packed;


int sclp_hsa_copy(void *, unsigned long, unsigned long);
int sclp_hsa_get_size(unsigned long *hsa_size);
void sclp_hsa_copy_init(void *);
void sclp_hsa_copy_exit();

#endif /* SCLP_H */
