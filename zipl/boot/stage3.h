/*
 * zipl - zSeries Initial Program Loader tool
 *
 * Main program for stage3 bootloader.
 *
 * Copyright IBM Corp. 2013
 * Author(s): Stefan Haberland <stefan.haberland@de.ibm.com>
 */

#ifndef STAGE3_H
#define STAGE3_H

#include "libc.h"
#include "s390.h"

#define IPL_DEVICE		 0x10404
#define INITRD_START		 0x10408
#define INITRD_SIZE		 0x10410
#define OLDMEM_BASE		 0x10418
#define OLDMEM_SIZE		 0x10420
#define COMMAND_LINE		 0x10480
#define COMMAND_LINE_SIZE	 896
#define COMMAND_LINE_EXTRA	 (0xA000-0x400)

#define STAGE3_FLAG_SCSI	 0x0001000000000000ULL
#define STAGE3_FLAG_KDUMP	 0x0002000000000000ULL

#define UNSPECIFIED_ADDRESS		-1ULL

extern unsigned long long _parm_addr;   /* address of parmline */
extern unsigned long long _initrd_addr; /* address of initrd */
extern unsigned long long _initrd_len;  /* length of initrd */
extern unsigned long long _load_psw;    /*  load psw of kernel */
extern unsigned long long _extra_parm;  /* use extra parm line mechanism? */
extern unsigned long long stage3_flags; /*  flags (e.g. STAGE3_FLAG_KDUMP) */
extern void kdump_stage3();

#endif /* STAGE3_H */
