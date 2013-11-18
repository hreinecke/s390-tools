/*
 * zgetdump - Tool for copying and converting System z dumps
 *
 * Write dump to standard output (stdout)
 *
 * Copyright IBM Corp. 2001, 2010
 * Author(s): Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#include "zgetdump.h"

int stdout_write_dump(void)
{
	u64 cnt, written = 0;
	char buf[32768];
	ssize_t rc;

	if (!dfi_feat_copy())
		ERR_EXIT("Copying not possible for %s dumps", dfi_name());
	STDERR("Format Info:\n");
	STDERR("  Source: %s\n", dfi_name());
	STDERR("  Target: %s\n", dfo_name());
	STDERR("\n");
	zg_progress_init("Copying dump", dfo_size());
	do {
		cnt = dfo_read(buf, sizeof(buf));
		rc = write(STDOUT_FILENO, buf, cnt);
		if (rc == -1)
			ERR_EXIT_ERRNO("Error: Write failed");
		if (rc != (ssize_t) cnt)
			ERR_EXIT("Error: Could not write full block");
		written += cnt;
		zg_progress(written);
	} while (written != dfo_size());
	STDERR("\n");
	STDERR("Success: Dump has been copied\n");
	return 0;
}
