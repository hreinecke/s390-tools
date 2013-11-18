/*
 * Stand-alone kdump support
 *
 * Copyright IBM Corp. 2012
 * Author(s): Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#ifndef __KDUMP_S_
#define __KDUMP_S_

#define LC_OS_INFO			0x0e18
#define OS_INFO_MAGIC_NUMBER		0x4f53494e464f535a # ASCII "OSINFOSZ"
#define OS_INFO_MAGIC			0x0000
#define OS_INFO_CSUM			0x0008
#define OS_INFO_VERSION_MAJOR		0x000c
#define OS_INFO_CRASH_BASE		0x0010
#define OS_INFO_CRASH_SIZE		0x0018
#define OS_INF0_CSUM_SIZE		4084
#define OS_INFO_VERSION_MAJOR_SUPPORTED	1
#define HSA_TMP_PAGE			0x5000

#include "error.h"

#if defined(__s390x__)

.macro kdump_base
_switch_31:
	sam31
	sr     %r1,%r1			# Erase register r1
	sr     %r2,%r2			# Erase register r2
	sigp   %r1,%r2,0x12		# Switch to 31 bit arch mode
	br     %r14

_switch_64:
	lhi    %r1,2			# mode 2 = esame (dump)
	lhi    %r0,0
	sigp   %r1,%r0,0x12		# switch to esame mode
	sam64				# switch to 64 bit mode
	larl   %r4,.Lr14_maskK1
	n      %r14,0(%r4)		# switch off 31 bit addressing bit
	larl   %r4,.LzerosK1
	lmh    %r14,%r15,0(%r4)		# clear high-order half
	br     %r14
.LzerosK1:
	.fill 2,4,0x0
.Lr14_maskK1:
	.long 0x7fffffff

_dump_failed:
	basr   %r13,0			# get base register
0:	st     %r2,.Lkdump_errnoK2-0b(%r13)

#ifdef USE_64BIT_SCLP
	larl   %r2,.Lmsg_dump_failedK2	# stage3 uses 64 bit sclp
	bras   %r14,_sclp_print
#else
	bras   %r14,_switch_31		# stage2 uses 31 bit sclp
	larl   %r2,.Lmsg_dump_failedK2
	bras   %r14,_sclp_print
	bras   %r14,_switch_64
#endif
	larl   %r13,.Lkdump_errnoK2
	llgf   %r2,0(%r13)
	bras   %r14,_panik

.Lkdump_errnoK2:
	.long  0x00000000
.Lmsg_dump_failedK2:
	# Dump failed: Check disabled wait code
	.byte 0xc4, 0xa4, 0x94, 0x97, 0x40, 0x86, 0x81, 0x89
	.byte 0x93, 0x85, 0x84, 0x7a, 0x40, 0xc3, 0x88, 0x85
	.byte 0x83, 0x92, 0x40, 0x84, 0x89, 0xa2, 0x81, 0x82
	.byte 0x93, 0x85, 0x84, 0x40, 0xa6, 0x81, 0x89, 0xa3
	.byte 0x40, 0x83, 0x96, 0x84, 0x85, 0x00

#
# Has to be called from 64 bit code
#
# parameter
#   %r2: Pointer to OS info
#
	.align 2
_os_info_check_64:
	stmg   %r6,%r15,48(%r15)	# save registers
	basr   %r13,0			# get base register
0:	aghi   %r15,-200		# create stack frame

	# Check for zero pointer
	ltgr   %r4,%r2			# Is pointer zero?
	jz     .Losinfo_missingK3	# yes, then exit

	# Check for page aligned
	tml    %r4,0xfff		# is page aligned?
	jnz    .Losinfo_missingK3	# no, then exit

	# Check if memory is backed
	tprot  0(%r4),0			# is address memory backed?
	jnz    .Losinfo_missingK3	# no, then exit

	# Check if magic is available
	lg     %r2,OS_INFO_MAGIC(%r4)	# do we find magic?
	clg    %r2,.Los_info_magicK3-0b(%r13)
	jne    .Losinfo_missingK3	# no, then exit

	# Check if checksum is ok
	sr     %r1,%r1			# zero accumulator
	la     %r2,OS_INFO_VERSION_MAJOR(%r4)
	lghi   %r3,OS_INF0_CSUM_SIZE
.LloopK3:
	cksm   %r1,%r2
	jnz    .LloopK3
	cl     %r1,OS_INFO_CSUM(%r4)
	jne    .Losinfo_csum_failedK3

	# Check if our major version is compatible
	lgh    %r2,OS_INFO_VERSION_MAJOR(%r4)
	lghi   %r3,OS_INFO_VERSION_MAJOR_SUPPORTED
	clgr   %r3,%r2			# is supported smaller than version?
	jl     .Losinfo_versionK3	# yes, then exit!

	# Check if crashkernel is set
	lg     %r2,OS_INFO_CRASH_BASE(%r4)
	ltgr   %r2,%r2			# is pointer zero?
	jz     .Losinfo_nocrashkernelK3	# yes, then exit

	lmg    %r6,%r15,248(%r15)
	br     %r14

.Losinfo_missingK3:
	lghi   %r2,EOS_INFO_MISSING
	j      .LpanicK3
.Losinfo_csum_failedK3:
	lghi   %r2,EOS_INFO_CSUM_FAILED
	j      .LpanicK3
.Losinfo_nocrashkernelK3:
	lghi   %r2,EOS_INFO_NOCRASHKERNEL
	j      .LpanicK3
.Losinfo_versionK3:
	lghi   %r2,EOS_INFO_VERSION
.LpanicK3:
	bras   %r14,_dump_failed

.Los_info_magicK3:
	.quad  OS_INFO_MAGIC_NUMBER

.endm /* kdump_base */

#
# Stage 2 kdump code
#
.macro kdump_stage2
#
# Copy crashkernel memory from [0, crashkernel size] to
# [crashkernel addr, crashkernel addr + crashkernel size], if
# R2 specifies a kdump boot menu entry
#
# parameter
#   %r2: Config number (starts with 1)
	.align 2
_kdump_stage2:
	stm    %r6,%r15,24(%r15)
	basr   %r13,0
0:	ahi    %r15,-200		# Create 64 bit stackframe

	# Get menu param area address and check if %R2 specifies
	# a kdump config number via config kdump bitmap
	l      %r6,.LparmareaM0-0b(%r13)
	lm     %r8,%r9,MENU_PARAM_CONFIG_KDUMP(%r6)
	srdl   %r8,0(%r2)
	tml    %r9,1			# is kdump bit set?
	jz     .LexitK4			# no, then exit

	# Switch to 64 bit mode
	bras   %r14,_switch_64
	basr   %r13,0			# New base register
0:
	# Get OS info
	lg     %r2,LC_OS_INFO(%r0)
	bras   %r14,_os_info_check_64

	lg     %r4,LC_OS_INFO(%r0)
	lg     %r2,OS_INFO_CRASH_BASE(%r4)
	lg     %r3,OS_INFO_CRASH_SIZE(%r4)

	# Copy crashkernel memory
	lghi   %r4,0
	lgr    %r5,%r3
.LloopK4:
	mvcle  %r2,%r4,0
	jo     .LloopK4

	# Relocate OS info pointer if necessary (needed for stage 3)
	lg     %r4,LC_OS_INFO(%r0)
	lg     %r2,OS_INFO_CRASH_BASE(%r4)
	lg     %r3,OS_INFO_CRASH_SIZE(%r4)
	clgr   %r3,%r4			# is OS info smaller than crash size?
	jl     .LnorelK4		# no, then no relocation
	agr    %r4,%r2			# yes, then add crashkernel base
	stg    %r4,LC_OS_INFO(%r0)	# and store pointer for stage 3
.LnorelK4:
	bras   %r14,_switch_31
.LexitK4:
	lm     %r6,%r15,224(%r15)
	br     %r14

.endm /* kdump_stage2 */

#
# Stage 3 kdump code
#
.macro kdump_stage3
	.align 2
_kdump_stage3:
	stm    %r6,%r15,24(%r15)		# save registers
	ahi    %r15,-200			# create stack frame (64 bit)

	bras   %r14,_switch_64
	basr   %r13,0				# get base register
0:
	larl   %r8,stage3_flags			# check for kdump flag
	lh     %r8,0(%r8)
	tml    %r8,STAGE3_FLAG_KDUMP
	jz     .LnokdumpK5

	tml    %r8,STAGE3_FLAG_SCSI		# check for SCSI flag
	jz     .LnoscsiK5

	# init SCLP HSA copy
	lghi   %r2,HSA_TMP_PAGE
	bras   %r14,_sclp_hsa_copy_init_64
	# get lowcore from HSA
	lghi   %r2,HSA_TMP_PAGE
	lghi   %r3,2
	lghi   %r4,1
	bras   %r14,_sclp_hsa_copy_64		# copy from HSA
	ltgr   %r2,%r2
	jnz    .Lstage3_failedK5		# rc !=0 -> panic

	lghi   %r10,HSA_TMP_PAGE
	lg     %r11,LC_OS_INFO(%r10)
	llgf   %r12,.Lhsa_sizeK5-0b(%r13)
	clgr   %r12,%r11			# is OS info in HSA?
	jl     .Los_info_checkK5		# no, then jump

	# get OS info page from HSA
	lghi   %r2,HSA_TMP_PAGE
	lgr    %r3,%r11
	srl    %r3,12
	ahi    %r3,2
	lghi   %r4,1
	bras   %r14,_sclp_hsa_copy_64		# copy from HSA
	ltgr   %r2,%r2
	jnz    .Lstage3_failedK5		# rc !=0 -> panic
	lghi   %r11,HSA_TMP_PAGE

.Los_info_checkK5:
	lgr    %r2,%r11
	bras   %r14,_os_info_check_64

	# Register crash base and size in kernel parameter
	lg     %r8,OS_INFO_CRASH_BASE(%r11)	# r8 = crash base
	lg     %r9,OS_INFO_CRASH_SIZE(%r11)	# r9 = crash size

.Lstart_copyK5:
	# copy memory to memory
	lgr    %r2,%r8
	lgr    %r3,%r9

	lghi   %r4,0
	lgr    %r5,%r3
.LloopK5:
	mvcle  %r2,%r4,0x00
	jo     .LloopK5

	# copy HSA to memory
	lgr    %r2,%r8				# copy first 32 MB from HSA
	lghi   %r3,2
	lghi   %r4,0x2000
	bras   %r14,_sclp_hsa_copy_64		# copy from HSA
	ltgr   %r2,%r2
	jnz    .Lstage3_failedK5		# rc !=0 -> panic

	# relocate os info pointer if necessary
	lg     %r4,LC_OS_INFO(%r8)
	clgr   %r9,%r4				# if crash size < OS pointer
	jl     .LnorelocK5			# jump (no relocation)
	agr    %r4,%r8				# otherwise:
	stg    %r4,LC_OS_INFO(%r0)		# OS pointer += crash size
.LnorelocK5:
	bras   %r14,_sclp_hsa_copy_exit_64
.LnoscsiK5:
	# Register oldmem in kernel parameter area
	lg     %r4,LC_OS_INFO(%r0)
	llgf   %r2,.Loldmem_baseK5-0b(%r13)	# load address of oldmem
	mvc    0(16,%r2),OS_INFO_CRASH_BASE(%r4)
	llgf   %r2,.Loldmem_sizeK5-0b(%r13)	# load address of oldmem size
	mvc    0(16,%r2),OS_INFO_CRASH_SIZE(%r4)
.LnokdumpK5:
	bras   %r14,_switch_31
	lm     %r6,%r15,224(%r15)
	br     %r14

.Lstage3_failedK5:
	bras   %r14,_sclp_hsa_copy_exit_64
	lghi   %r2,EHSA_COPY_FAILED
	bras   %r14,_dump_failed

	.align 4
.Loldmem_baseK5:
	.long OLDMEM_BASE
.Loldmem_sizeK5:
	.long OLDMEM_SIZE
.Lhsa_sizeK5:
	.long 0x2000000				# 32 MiB

.endm /* kdump_stage3 */

#else /* __s390x__ */

.macro kdump_base
.endm /* kdump_base */

.macro kdump_stage2
	.align 2
_kdump_stage2:
	br %r14
.endm /* kdump_stage2 */

.macro kdump_stage3
	.align 2
_kdump_stage3:
	br %r14
.endm /* kdump_stage3 */

#endif /* __s390x__ */
#endif /* __KDUMP_S_ */
