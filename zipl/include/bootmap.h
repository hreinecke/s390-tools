/*
 * s390-tools/zipl/include/bootmap.h
 *   Functions to build the bootmap file.
 *
 * Copyright IBM Corp. 2001, 2006.
 *
 * Author(s): Carsten Otte <cotte@de.ibm.com>
 *            Peter Oberparleiter <Peter.Oberparleiter@de.ibm.com>
 */

#ifndef BOOTMAP_H
#define BOOTMAP_H

#include "zipl.h"

#include "job.h"
#include "disk.h"


int bootmap_create(struct job_data* job, disk_blockptr_t* program_table,
		   disk_blockptr_t *scsi_dump_sb_blockptr,
		   disk_blockptr_t** stage2_list, blocknum_t* stage2_count,
		   char** device, struct disk_info** new_info);
void bootmap_store_blockptr(void* buffer, disk_blockptr_t* ptr,
			    struct disk_info* info);

#endif /* if not BOOTMAP_H */
