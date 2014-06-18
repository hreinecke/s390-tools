/*
 * util - Utility function library
 *
 * Partition detection functions
 *
 * Copyright IBM Corp. 2013
 */

#ifndef UTIL_PART_H
#define UTIL_PART_H

int util_part_search(const char *dev, size_t blk_start, size_t blk_cnt,
		     size_t blk_size, int *ext_part);
int util_part_search_fh(int fh, size_t blk_start, size_t blk_cnt,
			size_t blk_size, int *ext_part);

#endif /* UTIL_PART_H */
