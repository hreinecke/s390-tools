/*
 * zgetdump - Tool for copying and converting System z dumps
 *
 * Dump tool info generic functions
 *
 * Copyright IBM Corp. 2001, 2010
 * Author(s): Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#include "zgetdump.h"

/*
 * Supported dump tools
 */
static struct dt *dt_vec[] = {
	&dt_s390mv,
	&dt_s390sv,
	NULL,
};

/*
 * Dumper attribute information
 */
struct attr {
	int	*force;
	u64	*mem_limit;
	char	*dasd_type;
};

/*
 * File local static data
 */
struct {
	int		version;
	enum dfi_arch	arch;
	struct attr	attr;
	struct dt	*dt;
} l;

/*
 * Init dump tool backends
 */
void dt_init(void)
{
	struct dt *dt;
	int i = 0;

	while ((dt = dt_vec[i])) {
		g.fh = zg_open(g.opts.device, O_RDONLY, ZG_CHECK);
		if (zg_type(g.fh) != ZG_TYPE_DASD)
			ERR_EXIT("Please specify DASD device node (e.g. "
				 "/dev/dasdd)");
		if (dt->init() == 0) {
			l.dt = dt;
			return;
		}
		zg_close(g.fh);
		i++;
	}
	ERR_EXIT("No dump tool found on \"%s\"", g.opts.device);
}

/*
 * Print info about dump tool
 */
void dt_info_print(void)
{
	STDERR("Dump device info:\n");
	STDERR("  Dump tool.........: %s\n", l.dt->desc);
	STDERR("  Version...........: %d\n", l.version);
	STDERR("  Architecture......: %s\n", dfi_arch_str(l.arch));
	if (l.attr.dasd_type)
		STDERR("  DASD type.........: %s\n", l.attr.dasd_type);
	if (l.attr.mem_limit)
		STDERR("  Dump size limit...: %lld MB\n",
		       TO_MIB(*l.attr.mem_limit));
	else
		STDERR("  Dump size limit...: none\n");
	if (l.attr.force) {
		if (*l.attr.force == 0)
			STDERR("  Force specified...: no\n");
		else
			STDERR("  Force specified...: yes\n");
	}
	if (l.dt->info) {
		STDERR("\n");
		l.dt->info();
	}
}

/*
 * Set DT architecture
 */
void dt_arch_set(enum dfi_arch arch)
{
	l.arch = arch;
}

/*
 * Set DT version
 */
void dt_version_set(int version)
{
	l.version = version;
}

/*
 * Set DT memory limit attribute
 */
void dt_attr_mem_limit_set(u64 mem_limit)
{
	l.attr.mem_limit = zg_alloc(sizeof(*l.attr.mem_limit));
	*l.attr.mem_limit = mem_limit;
}

/*
 * Set DT force attribute
 */
void dt_attr_force_set(int force)
{
	l.attr.force = zg_alloc(sizeof(*l.attr.force));
	*l.attr.force = force;
}

/*
 * Set DT DASD type attribute
 */
void dt_attr_dasd_type_set(const char *dasd_type)
{
	l.attr.dasd_type = zg_strdup(dasd_type);
}
