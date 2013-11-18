/*
 * hyptop - Show hypervisor performance data on System z
 *
 * Common functions for debugfs data gatherer
 *
 * Copyright IBM Corp. 2010
 * Author(s): Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "helper.h"
#include "hyptop.h"
#include "dg_debugfs.h"

static char *l_debugfs_dir;

static void l_check_rc(int rc, int exit_on_err)
{
	if (!exit_on_err)
		return;
	if (rc == -EACCES)
		ERR_EXIT("Permission denied, check \"%s/s390_hypfs/\"\n",
			 l_debugfs_dir);
	if (rc != -ENOENT)
		ERR_EXIT("Could not initialize data gatherer (%s)\n",
			 strerror(-rc));
}

static void l_check_rc_final(int rc, int exit_on_err)
{
	if (!exit_on_err)
		return;
	l_check_rc(rc, exit_on_err);
	ERR_EXIT("Could not initialize data gatherer (%s)\n", strerror(-rc));
}

/*
 * Initialize debugfs data gatherer backend
 */
int dg_debugfs_init(int exit_on_err)
{
	int rc;

	l_debugfs_dir = ht_mount_point_get("debugfs");
	if (!l_debugfs_dir) {
		if (!exit_on_err)
			return -ENODEV;
		ERR_EXIT("Debugfs is not mounted, try \"mount none -t debugfs "
				 "/sys/kernel/debug\"\n");
	}
	rc = dg_debugfs_vm_init();
	if (rc == 0)
		return 0;
	else
		l_check_rc(rc, exit_on_err);
	rc = dg_debugfs_lpar_init();
	if (rc == 0)
		return 0;
	else
		l_check_rc_final(rc, exit_on_err);
	return rc;
}

/*
 * Open a debugfs file
 */
int dg_debugfs_open(const char *file)
{
	char path[PATH_MAX];
	int fh;

	path[0] = 0;
	strcat(path, l_debugfs_dir);
	strcat(path, "/s390_hypfs/");
	strcat(path, file);
	fh = open(path, O_RDONLY);
	if (fh == -1)
		return -errno;
	else
		return fh;
}

