/*
 * zipl - zSeries Initial Program Loader tool
 *
 * Stand-alone kdump support (stage 2)
 *
 * Copyright IBM Corp. 2013
 */

#include "libc.h"
#include "stage2.h"
#include "error.h"
#include "menu.h"
#include "kdump.h"

static struct os_info **lc_os_info = ((struct os_info **)&S390_lowcore.os_info);

/*
 * Copy crashkernel memory from [0, crash size] to
 * [crash base, crash base + crash size] if config_nr specifies a
 * kdump boot menu entry.
 *
 * Parameter:
 *   Config number (starts with 1)
 */
void kdump_stage2(unsigned long config_nr)
{
	struct os_info *os_info = *lc_os_info;
	unsigned long crash_size;
	void *crash_base;

	if (!(__stage2_params.config_kdump & (0x1 << config_nr)))
		return;
	os_info_check(os_info);

	/* Copy crashkernel memory */
	crash_base = (void *) os_info->crashkernel_addr;
	crash_size = os_info->crashkernel_size;
	memcpy(crash_base, NULL, crash_size);
	/*
	 * Relocate OS info pointer if necessary (needed for stage 3)
	 * If OS info is smaller than crash size then add crash base
	 */
	if (__pa(os_info) >= crash_size)
		return;
	*lc_os_info = (void *) __pa(os_info) + __pa(crash_base);
}
