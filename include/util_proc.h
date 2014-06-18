/*
 *   Scanner for the /proc files
 *
 * Copyright IBM Corp. 2001, 2007.
 *
 * Author(s): Peter Oberparleiter <Peter.Oberparleiter@de.ibm.com>
 */

#ifndef PROC_H
#define PROC_H

#include <ctype.h>
#include <sys/types.h>


struct util_proc_part_entry {
	dev_t device;
	size_t blockcount;
	char *name;
};

struct util_proc_dev_entry {
	int blockdev;
	dev_t device;
	char *name;
};


int util_proc_part_get_entry(dev_t device, struct util_proc_part_entry *entry);
void util_proc_part_free_entry(struct util_proc_part_entry *entry);
int util_proc_dev_get_entry(dev_t dev, int blockdev,
			    struct util_proc_dev_entry *entry);
void util_proc_dev_free_entry(struct util_proc_dev_entry *entry);

#endif /* not PROC_H */
