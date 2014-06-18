/*
 *  Entry module for stage 2 stand-alone dump tools
 *
 *  Copyright IBM Corp. 2001, 2013
 *  Author(s): Michael Holzheu  <holzheu@linux.vnet.ibm.com>
 */

#define __LC_SAVE_AREA_SYNC	0x200	/* Save area for interrupts */
#define __LC_PGM_OLD_PSW	0x0150	/* Program check old PSW */

.globl pgm_check_handler
pgm_check_handler:
	stmg	%r0,%r15,__LC_SAVE_AREA_SYNC
	aghi	%r15,-200
	larl	%r14,pgm_check_handler_fn
	basr	%r14,%r14
	lmg	%r0,%r15,__LC_SAVE_AREA_SYNC
	lpswe	__LC_PGM_OLD_PSW(%r0)
