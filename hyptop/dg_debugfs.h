/*
 * hyptop - Show hypervisor performance data on System z
 *
 * Common functions for debugfs data gatherer
 *
 * Copyright IBM Corp. 2010
 * Author(s): Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#ifndef DG_DEBUGFS_H
#define DG_DEBUGFS_H

#define DBFS_WAIT_TIME_US 10000

extern int dg_debugfs_init(int exit_on_err);
extern int dg_debugfs_vm_init(void);
extern int dg_debugfs_lpar_init(void);
extern int dg_debugfs_open(const char *file);

#endif /* DG_DEBUGFS_H */
