/*
 * zgetdump - Tool for copying and converting System z dumps
 *
 * S390 multi-volume DASD dump tool
 *
 * Copyright IBM Corp. 2001, 2010
 * Author(s): Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#include "zgetdump.h"

/*
 * DT operations
 */
struct dt dt_s390mv = {
	.desc	= "Multi-volume DASD dump tool",
	.init	= dt_s390mv_init,
	.info	= dt_s390mv_info,
};
