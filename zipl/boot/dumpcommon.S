/*
 *  dumpcommon.S
 *
 *  Common routines for dump records
 *    Copyright IBM Corp. 2001, 2006.
 *    Author(s): Michael Holzheu  <holzheu@de.ibm.com>
 *
 * Uses extern functions:
 *  - _dump_mem (device dependent function to write dump)
 *
 * Functions:
 *  - _enable_device
 *  - _ssch
 *  - _wait4de
 *  - _panik
 *  - _take_dump
 *  - _store_status
 */

#include "error.h"

/* General defines */

#define PAGE_SIZE        0x1000                /* 4096 */
#define HEADER_SIZE      0x1000                /* 4096 */
#define END_MARKER_SIZE  0x10
#define DUMP_END_MARKER  0x44554d50,0x5f454e44 /* DUMP_END */
#define IPL_SC           0xb8                  /* Address of ipl subchannel */
#define S390_DUMP_MAGIC  0xa8190173,0x618f23fd /* magic number */
#define ARCH_S390_ID     0x1                   /* arch flag for s390  */
#define ARCH_S390X_ID    0x2                   /* arch flag for s390x */

#define __LC_ARCH_MODE_ID 163        /* here is the arch flag in the lowcore */
#define __LC_IPIB         0xe00      /* IPL Parameter Information Block */
#define __LC_CPU_ADDRESS  168        /* CPU address in lowcore */
#define DIAG308_IPL       3          /* Subcode 3 - Perform Load Clear  */
#define DIAG308_SET       5          /* Subcode 5 - Set IPL Parameters  */

#define PARAM_START        0x4000  /* 8-byte time stamp plus 2-byte count  */
                                   /* plus 32 13-byte entries              */
#define IDA_LIST_START     0x4200  /* 64 8-byte IDAW's                     */
#define CCW_CHAIN_START    0x4400  /* chained write CCW's                  */
#define ZERO_MEM_START     0x6000  /* Zero pages start */
#define ZERO_MEM_SIZE      0x3000  /* Init three zero pages */

#define SCPINCR1_OFF   8
#define SCPA1_OFF      10
#define SCPA2_OFF      100
#define SCPINCR2_OFF   104


#define ZERO_PAGE_START    0x6000 /* Zero page */
#define HEADER_PAGE_START  0x5000 /* Dump header page */
#define PREFIX_ARR_START   0x5800 /* Prefix page array in dump header */
#define TMP_PAGE_START     0x7000 /* Page for temp storage */

################################################################################
# MACRO: dump_header
################################################################################

.macro dump_header
.align 8

#
# The Dump header
#
.Ldh_dumpheader:
.Ldh_magic_number:.long S390_DUMP_MAGIC
.Ldh_version:     .long 0x00000005
.Ldh_header_size: .long HEADER_SIZE
.Ldh_dump_level:  .long 0x00000004              # DUMP_ALL
.Ldh_page_size:   .long PAGE_SIZE
.Ldh_mem_size:    .long 0x00000000,0x00000000
.Ldh_mem_start:   .long 0x00000000,0x00000000
.Ldh_mem_end:     .long 0x00000000,0x00000000
.Ldh_num_pages:   .long 0x00000000
.Ldh_pad:         .long 0x00000000
.Ldh_time:        .long 0x00000000,0x00000000
.Ldh_cpuid:       .long 0x00000000,0x00000000
.Ldh_arch:        .long ARCH_S390_ID
.Ldh_vol_nr:      .long 0x00000000
#if defined(__s390x__)
.Ldh_build_arch:  .long ARCH_S390X_ID
#else
.Ldh_build_arch:  .long ARCH_S390_ID
#endif
.Ldh_real_mem_size: .long 0x00000000,0x00000000
.Ldh_mvdump:      .byte 0x00
.Ldh_cpu_cnt:     .byte 0x00,0x00
.Ldh_real_cpu_cnt:.byte 0x00,0x00

#
# Dump End Marker
#
.align 8
.Ld_endmarker:    .long DUMP_END_MARKER
.Ld_end_time:     .long 0x00000000,0x00000000

.endm  /* dump_header */

.macro hex_to_ebcdic
.align 2
################################################################################
#  Translate binary hex to decimal ebcdic
#   -r2: value (bin)
#   -r3: outbuffer (ebcdic - decimal), 8 byte
################################################################################

_hex_to_ebcdic:
	stm    %r6,%r15,24(%r15)
	basr   %r13,0                          # base register
0:	cvd   %r2,.Ltmp_data-0b(%r13)          # convert to packed decimal
	unpk  0(8,%r3),.Ltmp_data+4-0b(4,%r13)
	oi    7(%r3),0xf0                      # remove signed flag
	lm    %r6,%r15,24(%r15)
	br    %r14

.endm



#if defined(__s390x__)


/******************************** 64 BIT **************************************/

.macro print_progress_64
.align 2

################################################################################
#  Init dump progress messages to sclp console
#  (8 messages if the memory is 4G or less, otherwise one message for each
#  512M chunk of dumped memory)
################################################################################
_init_print_progress_64:
	stmg   %r6,%r15,48(%r15)
	basr   %r13,0                   # base register
0:	aghi   %r15,-200

	lg     %r8,.Ldh_mem_size-0b(%r13)	
	srlg   %r2,%r8,20
	la     %r3,.Lmsg_mem_size-0b(%r13)
	bras   %r14,_hex_to_ebcdic
	srlg   %r2,%r8,3
	lgf    %r3,.Lprogress_increment-0b(%r13)
	clgr   %r2,%r3
	bl     1f-0b(%r13)
	lgr    %r2,%r3
1:      st     %r2,.Lprogress_increment-0b(%r13)
	stg    %r2,.Lnext_print_addr-0b(%r13)

	lmg     %r6,%r15,248(%r15)
	br      %r14

################################################################################
#  Print dump progress to sclp console
#   -r2: memory location in bytes
################################################################################
_print_progress_64:
	stmg   %r6,%r15,48(%r15)
	basr   %r13,0                   # base register
0:      aghi   %r15,-200

	# check, if we have to print something

	lg      %r9,.Lnext_print_addr-0b(%r13)
	clgr    %r2,%r9
	bl      .Lexit_progress-0b(%r13)

	# print MBs to console

	srlg    %r2,%r2,20
	la      %r3,.Lmsg_progress_mb-0b(%r13)
	bras    %r14,_hex_to_ebcdic
	la      %r2,.Lmsg_progress_mb-0b(%r13)
	bras    %r14,_sclp_print

	# update address for next print

	agf     %r9,.Lprogress_increment-0b(%r13)
	stg     %r9,.Lnext_print_addr-0b(%r13)
	
.Lexit_progress:
	lmg     %r6,%r15,248(%r15)
	br      %r14
.Lnext_print_addr:
	.quad 0x0
.Lprogress_increment:
	.long 0x20000000        # issue progress message at least each 512M

.endm

################################################################################
# MACRO: dump_io_subroutines_64
# - _enable_device_64
# - _ssch_64
# - _wait4de_64
# - _panik_64
################################################################################

.macro dump_io_subroutines_64
.align 2

################################################################################
# Enable I/O on the ipl device.
#   -r2 : device subchannel id
################################################################################

_enable_device_64:
        stmg   %r6,%r15,48(%r15)
        basr   %r13,0                     # base register
0:      aghi   %r15,-200
        lgr    %r1,%r2
        lghi   %r2,EENABLE_DEV            # set panik code early
        stsch  .Ltmp_data-0b(%r13)
        oi     .Ltmp_data-0b+5(%r13),0x84 # enable ssch and multipath mode
        msch   .Ltmp_data-0b(%r13)
        bnz    _panik_64-0b(%r13)         # subchannel busy or in error ?
        lctl   %c6,%c6,1f-0b(%r13)        # enable all interrupt classes
        lmg    %r6,%r15,248(%r15)
        br     %r14
        .align 8
1:      .long  0xff000000                 # CR6 initial value

################################################################################
# Start I/O
#  -r2: device subchannel id
#  -r3: address of orb
#  -r4: address of irb
#  -r5: retry count
################################################################################

_ssch_64:
        stmg   %r6,%r15,48(%r15)
        basr   %r13,0                    # base register
0:      aghi   %r15,-200                 # create stack frame
	lghi   %r8,10                    # set retry count for ssch
        lgr    %r12,%r2                  # save subchannel id
        lgr    %r11,%r3                  # save orb
        lgr    %r10,%r4                  # save irb
        lgr    %r9,%r5                   # save retry count
1:      lgr    %r1,%r12
        ssch   0(%r11)                   # go
	bz     2f-0b(%r13)
	tsch   0(%r4)
	bct    %r8,1b-0b(%r13)           # do retry
        bnz    5f-0b(%r13)               # houston, we have a problem
2:      lgr    %r2,%r12                  # call _wait4de with subchannel id
        lgr    %r3,%r10                  # and irb address as parameters
        bas    %r14,_wait4de_64-0b(%r13) # wait until DE or error
        tm     9(%r10),0xff              # test channel status
        bz     3f-0b(%r13)
        bct    %r9,1b-0b(%r13)           # something went wrong, retry.
3:      tm     8(%r10),0xd2              # test device status
        bz     4f-0b(%r13)
        bct    %r9,1b-0b(%r13)           # something went wrong, retry.
        j      5f                        # retries failed -> panic

4:      lmg    %r6,%r15,248(%r15)
        br     %r14

5:      lghi   %r2,ESSCH
        bas    %r4,_panik_64-0b(%r13)

################################################################################
# Wait for interrupt subroutine
#  -r2: device subchannel id
#  -r3: address of irb
################################################################################

_wait4de_64:
        stmg   %r6,%r15,48(%r15)
        basr   %r13,0                    # base register
0:      aghi   %r15,-200                 # create stack frame
        lr     %r1,%r2
        mvc    496(16),6f-0b(%r13)       # set i/o new psw
1:      lpswe  5f-0b(%r13)
2:      stnsm  .Lpsw-0b(%r13),0xfd       # disable io interrupts
        c      %r1,0xb8                  # compare subchannel id
        bne    1b-0b(%r13)               # unequal -> continue waiting
        tsch   0(%r3)
        tm     9(%r3),0xff               # test channel status
        bnz    4f-0b(%r13)

3:      tm     8(%r3),0x02               # got unit check ?
        bnz    4f-0b(%r13)

        tm     8(%r3),0x04               # got device end ?
        bz     1b-0b(%r13)               # still busy -> continue waiting

4:      lmg   %r6,%r15,248(%r15)
        br     %r14
        .align 16
5:      .long  0x02020001,0x80000000,0x00000000,0x00000000+1b
6:      .long  0x00000001,0x80000000,0x00000000,0x00000000+2b
.Lpsw:  .long  0x0

################################################################################
# Panik routine. Loads a disabled wait psw or performs reipl if required.
#   -r2 : panik code
################################################################################

_panik_64:
        stmg   %r6,%r15,48(%r15)
        basr   %r13,0                    # base register
0:      aghi   %r15,-200                 # create stack frame
	stg    %r2,1f-0b+8(%r13)         # store code in address part of psw
	bras   %r14,_print_exit_message

	# perform reipl: check lowcore for the address of an IPL Information
	# Block followed by a valid checksum (as defined in lowcore.h and set
	# by ipl.c). In case of match use diag308 to IPL.

	lg    %r2,__LC_IPIB(%r0)                 # IPL Information Block
	ltr   %r2,%r2                            # re-ipl required?
	bz    .Lnoreipl-0b(%r13)                 # no, skip diag308
	llgf  %r3,0(%r2)                         # length of IPIB
	sr    %r1,%r1                            # zero accumator
.Lcksmloop:
	cksm  %r1,%r2                            # compute IPIB checksum
	bnz   .Lcksmloop-0b(%r13)                # repeat if not complete
	cl    %r1,__LC_IPIB+8(%r0)               # compare with cksm in lowcore
	bne   .Lnoreipl-0b(%r13)                 # no match, skip diag308
	lg    %r2,__LC_IPIB(%r0)                 # reload IPL Information Block
	lghi  %r4,DIAG308_SET                    # load subcode
	diag  %r2,%r4,0x308                      # set IPL parameters
	lghi  %r4,DIAG308_IPL                    # load subcode
	diag  %r2,%r4,0x308                      # perform load clear
.Lnoreipl:
	lpswe  1f-0b(%r13)
        .align 8
1:      .long  0x00020000,0x80000000,0x00000000,0x00000000
.endm

################################################################################
# MACRO: dump_common_fn_64
# - _take_dump_64
# - _count_mem_64
# - _store_status_64
# - _copy_lowcore_64
################################################################################

.macro dump_common_fn_64
.align 2

################################################################################
# Take the dump
#  - no parameters
################################################################################

.align 2
_take_dump_64:
        stmg  %r6,%r15,48(%r15)
        basr  %r13,0
.Lbase: aghi  %r15,-200                          # create stack frame

	sckc  .Lclock_comparator-.Lbase(%r13)    # set clock comparator to future
	spt   .Lcpu_timer-.Lbase(%r13)           # set cpu timer to future

	lghi  %r6,ZERO_MEM_START                 # clear memory
	lgr   %r7,%r6
	aghi  %r7,ZERO_MEM_SIZE
	sgr   %r7,%r6
	sgr   %r8,%r8
	sgr   %r9,%r9
	mvcle %r6,%r8,0
	jo    .-4

	# initialize sclp

	lghi   %r2,0
	bras   %r14,_sclp_setup

	# print start message

	la     %r2,.Lmsg_start-.Lbase(%r13)
	bras   %r14,_sclp_print

	# print OS type

	la     %r2,.Lmsg_64bit_os-.Lbase(%r13)
	l      %r10,.Ldh_arch-.Lbase(%r13)
	chi    %r10,ARCH_S390_ID
	bnz    .Larch64-.Lbase(%r13)
	la     %r2,.Lmsg_31bit_os-.Lbase(%r13)
.Larch64:
	bras   %r14,_sclp_print

	# count memory

	bas   %r14,_count_mem_64-.Lbase(%r13)

	# copy dump header

	stck    .Ldh_time-.Lbase(%r13)            # store time
	stidp   .Ldh_cpuid-.Lbase(%r13)           # store CPU ID

	lghi    %r3,HEADER_PAGE_START
	mvc     0(256,%r3),.Ldh_dumpheader-.Lbase(%r13)

	# dump memory

        llgf  %r14,.Ldump_mem_64-.Lbase(%r13)
        basr  %r14,%r14                          # dump memory

	# exit

        lghi  %r2,OK
        bas   %r14,_panik_64-.Lbase(%r13)        # everything ok: stop now

.align 8
.Lclock_comparator:
	.quad  0xffffffffffffffff
.Lcpu_timer:
	.quad  0x7fffffffffffffff

################################################################################
# Find out memory size:
# Use Read SCP or Read SCP forced to do this
#  - no parameters
################################################################################

.align 2
_count_mem_64:
        stmg  %r6,%r15,48(%r15)
        basr  %r13,0                          # base register
0:      aghi  %r15,-200                       # create stack frame

	lghi %r2,0
	bras %r14,_sclp_read_info
	chi  %r2,0
	bne  .Lsclp_read_info_ok-0b(%r13)
	lghi  %r2,EMEMCOUNT
	bras  %r14,_panik_64
.Lsclp_read_info_ok:
	lgr   %r12,%r2
.Lprocsccb:
        lghi  %r1,0
	icm   %r1,3,SCPINCR1_OFF(%r12)  # use this one if != 0
        jnz   .Lscnd
        lg    %r1,SCPINCR2_OFF(%r12)    # otherwise use this one
.Lscnd:
        xgr   %r3,%r3                   # same logic
        ic    %r3,SCPA1_OFF(%r12)
        chi   %r3,0x00
        jne   .Lcompmem
        l     %r3,SCPA2_OFF(%r12)
.Lcompmem:
        mlgr  %r2,%r1                   # mem in MB on 128-bit
        l     %r1,.Lonemb-0b(%r13)
        mlgr  %r2,%r1                   # mem size in bytes in %r3

        stg   %r3,.Ldh_real_mem_size-0b(%r13)
        larl  %r7,.Lmem_upper_limit
        lg    %r6,0(%r7)                     # check if we have an upper limit
        clgr  %r3,%r6
        bl    .Lsavemem-0b(%r13)
        lgr   %r3,%r6                        # upper mem limit set -> use it!
.Lsavemem:
        stg   %r3,.Ldh_mem_size-0b(%r13)     # store memory size
        stg   %r3,.Ldh_mem_end-0b(%r13)      # store memory end
        srlg  %r12,%r3,12                    # calculate page count (/ 4096)
        st    %r12,.Ldh_num_pages-0b(%r13)   # store page count

        clgr   %r6,%r3
        bne   .Lexit-0b(%r13)
        larl  %r2,.Lmsg_mem_limit_set        # print mem limit warning
        bras  %r14,_sclp_print
.Lexit:
        lmg   %r6,%r15,248(%r15)
        br    %r14
.Lonemb:
        .int 0x100000


################################################################################
# store status of all cpus in their lowcores
#  - no parameters
################################################################################

_store_status_64:
        stmg  %r6,%r15,48(%r15)
        basr  %r13,0                          # base register
0:      aghi  %r15,-200

        ######## stop all cpus and store status in prefix pages ########

        lghi  %r8,0                           # first cpu
        stap  .Lcurrent_cpu_64+2-0b(%r13)     # store current cpu address
        llgh  %r6,.Lcurrent_cpu_64+2-0b(%r13)

        ######## move lowcore info (assume user has made store  ########
        ######## status) to prefix-page                         ########

        sth   %r6,__LC_CPU_ADDRESS(%r0)
        bas   %r14,_copy_lowcore_64-0b(%r13)

1:
        cl    %r8,.Lcurrent_cpu_64-0b(%r13)   # is ipl cpu ?
        be    4f-0b(%r13)                     # if yes get next cpu
2:
        lghi %r9,0
        sigp %r9,%r8,0x9                      # store status of cpu
        bc   8,3f-0b(%r13)                    # accepted
        bc   4,4f-0b(%r13)                    # status stored: next cpu
        bc   2,2b-0b(%r13)                    # busy:          try again
        bc   1,4f-0b(%r13)                    # not op:        next cpu
3:
        sth   %r8,__LC_CPU_ADDRESS(%r0)
        bas   %r14,_copy_lowcore_64-0b(%r13)
4:
        aghi  %r8,1                           # next cpu (r8 +=1)
        cl    %r8,.Llast_cpu_64-0b(%r13)      # is last possible cpu ?
        bl    1b-0b(%r13)                     # jump if not last cpu

        lmg   %r6,%r15,248(%r15)
        br    %r14                            # return to caller
.Lcurrent_cpu_64:
	.long 0x0
.Llast_cpu_64:
        .long 0x0000ffff

################################################################################
# copy lowcore 64
#  - no parameters
################################################################################

_copy_lowcore_64:
        stmg   %r6,%r15,48(%r15)
        basr   %r13,0                         # base register
0:      aghi   %r15,-200

        lghi  %r2,0x1000                      # offset for first page
        llgf  %r3,792(%r2)                    # get prefix page of current cpu

        ###### check if lowcore address looks valid ######

        cl    %r3,.Llinux_start_64-0b(%r13)   # looks valid ?
        bl    .Lcpy_locore_exit_64-0b(%r13)   # if < linux-start addr
        llgf  %r6,.Lpage_align_64-0b(%r13)    # check page alignment
        nr    %r3,%r6
        cl    %r3,792(%r2)                    # 4888
        bnz   .Lcpy_locore_exit_64-0b(%r13)   # if not page aligned

        ###### copy lowcore                         ######

        llgf  %r3,792(%r2)                    # get prefix page of current cpu

        lgh   %r8,.Ldh_cpu_cnt-0b(%r13)       # Save lowcore pointer (32 bit)
        sll   %r8,2                           # in dump header
        lghi  %r9,PREFIX_ARR_START
        agr   %r9,%r8
        st    %r3,0(%r9)

        lgh   %r8,__LC_CPU_ADDRESS(%r0)       # copy cpu address
        sth   %r8,__LC_CPU_ADDRESS(%r3)       # to lowcore

        aghi  %r3,0x1000                      # get base register for second
                                              # page of prefix pages

        # |-----------------------------------------------------------|
        # | Decimal |  Length   | Data                                |
        # | Address |  in Bytes |                                     |
        # |_________|___________|_____________________________________|
        # | 163     | 1         | Architectural Mode ID               |
        # | 4608    | 128       | Fl-pt registers 0-15                |
        # | 4736    | 128       | General registers 0-15              |
        # | 4864    | 16        | Current PSW                         |
        # | 4888    | 4         | Prefix register                     |
        # | 4892    | 4         | Fl-pt control register              |
        # | 4900    | 4         | TOD programmable register           |
        # | 4904    | 8         | CPU timer                           |
        # | 4912    | 1         | Zeros                               |
        # | 4913    | 7         | Bits 0-55 of clock comparator       |
        # | 4928    | 64        | Access registers 0-15               |
        # | 4992    | 128       | Control registers 0-15              |
        # |_________|___________|_____________________________________|

        mvc   512(256,%r3),512(%r2)      # 4608
        mvc   768(16,%r3),768(%r2)
        mvc   792(8,%r3),792(%r2)        # 4888
        mvc   804(20,%r3),804(%r2)       # 4900
        mvc   832(192,%r3),832(%r2)      # 4928

        lgh   %r8,.Ldh_cpu_cnt-0b(%r13)       # Increment (online) CPU count
        aghi  %r8,1
        sth   %r8,.Ldh_cpu_cnt-0b(%r13)
.Lcpy_locore_exit_64:
        lgh   %r10,.Ldh_real_cpu_cnt-0b(%r13) # Increment real CPU count
        aghi  %r10,1
        sth   %r10,.Ldh_real_cpu_cnt-0b(%r13)

        lmg   %r6,%r15,248(%r15)
        br    %r14                            # return to caller
.Lpage_align_64:
        .long -4096
.Llinux_start_64:
        .long  0x10000

.align 4
.Ldump_mem_64:    .long _dump_mem_64          # address of function

.endm /* dump_common_fn_64 */


################################################################################
# MACRO: dump_idal_64
# - _create_ida_list_64
################################################################################

.macro dump_idal_64
.align 2

################################################################################
# This function creates a indirect data addressing list
# Parameters:
#  -r2:  start address
#  -r3:  length
#  -r4:  address of ida list
################################################################################
_create_ida_list_64:
        stmg    %r6,%r15,48(%r15)
        basr    %r13,0                        # base register
0:      aghi    %r15,-200                     # create stack frame

        # setup new pgc psw for finding invalid pages

        mvc     .Ltmp_data-0b(16,%r13),464(%r0)  # save old psw
        mvc     464(16,%r0),.Lpage_invalid_psw-0b(%r13) # setup pgm check new

        lgr     %r14,%r4                      # copy address of ida list
        lgr     %r11,%r2                      # copy start address
        lgr     %r9,%r3                       # copy length
        agr     %r9,%r11                      # compute end address
1:
        l       %r7,0(%r11)                   # test page
        lgr     %r7,%r11
        b       .Lpage_ok-0b(%r13)
.Lpage_invalid:
        lghi    %r7,ZERO_PAGE_START
.Lpage_ok:
        stg     %r7,0(%r14)                   # store address
        aghi    %r14,8                        # update ida pointer
        aghi    %r11,4096                     # update data address
        clgr    %r11,%r9                      # enough ?
        bl      1b-0b(%r13)                   # branch if r11 < r9

        mvc     464(16,%r0),.Ltmp_data-0b(%r13) # restore pgm check new
        lmg     %r6,%r15,248(%r15)
        br      %r14                          # return to caller
.Lpage_invalid_psw:
        .long 0x00000001,0x80000000,0x00000000,0x00000000 + .Lpage_invalid

.endm /* dump_idal_64 */


#else /* __s390x__ */



/******************************** 32 BIT **************************************/

.macro print_progress_32
.align 2

################################################################################
#  Init dump progress messages to sclp console (8 messages for all the memory)
#   - no parameters
################################################################################
_init_print_progress_32:
	stm    %r6,%r15,24(%r15)
	basr   %r13,0                   # base register
0:	ahi    %r15,-96

	l      %r2,.Ldh_mem_size+4-0b(%r13)	
	srl    %r2,20
	la     %r3,.Lmsg_mem_size-0b(%r13)
	bras   %r14,_hex_to_ebcdic
	l      %r8,.Ldh_mem_size+4-0b(%r13)	
	srl    %r8,3
	st     %r8,.Lnext_print_addr-0b(%r13)

	lm     %r6,%r15,120(%r15)
	br     %r14

################################################################################
#  Print dump progress to sclp console (8 messages for all the memory)
#   -r2: memory location in bytes
################################################################################
_print_progress_32:
	stm    %r6,%r15,24(%r15)
	basr   %r13,0                   # base register
0:	ahi    %r15,-96

	# check, if we have to print something

	l       %r9,.Lnext_print_addr-0b(%r13)
	clr     %r2,%r9
	bl      .Lexit_progress-0b(%r13)

	# print MBs to console

	srl     %r2,20
	la      %r3,.Lmsg_progress_mb-0b(%r13)
	bras    %r14,_hex_to_ebcdic
	la      %r2,.Lmsg_progress_mb-0b(%r13)
	bras    %r14,_sclp_print

	# update address for next print

	l       %r8,.Ldh_mem_size+4-0b(%r13)	
	srl     %r8,3
	ar      %r9,%r8
	st      %r9,.Lnext_print_addr-0b(%r13)
	
.Lexit_progress:
	lm     %r6,%r15,120(%r15)
	br     %r14
.Lnext_print_addr:
	.long 0x0

.endm


################################################################################
# MACRO: dump_io_subroutines_32
# - _enable_device_32
# - _ssch_32
# - _wait4de_32
################################################################################

.macro dump_io_subroutines_32
.align 2

################################################################################
# Enable I/O on the ipl device.
#   -r2 : device subchannel id
################################################################################

_enable_device_32:
        stm    %r6,%r15,24(%r15)
        basr   %r13,0                   # base register
0:      ahi    %r15,-96
        lr     %r1,%r2
        lhi    %r2,EENABLE_DEV          # set panik code early
        stsch  .Ltmp_data-0b(%r13)
        oi     .Ltmp_data-0b+5(%r13),0x84 # enable ssch and multipath mode
        msch   .Ltmp_data-0b(%r13)
        bnz    _panik_32-0b(%r13)       # subchannel busy or in error ?
        lctl   %c6,%c6,1f-0b(%r13)      # enable all interrupt classes
        lm     %r6,%r15,120(%r15)
        br     %r14
        .align 8
1:      .long  0xff000000               # CR6 initial value

################################################################################
# Start I/O
#  -r2: device subchannel id
#  -r3: address of orb
#  -r4: address of irb
#  -r5: retry count
################################################################################

_ssch_32:
        stm    %r6,%r15,24(%r15)
        basr   %r13,0                    # base register
0:      ahi    %r15,-96                  # create stack frame
	lhi    %r8,10                    # set retry count for ssch
        lr     %r12,%r2                  # save subchannel id
        lr     %r11,%r3                  # save orb
        lr     %r10,%r4                  # save irb
        lr     %r9,%r5                   # save retry count
1:      lr     %r1,%r12
        stnsm  .Lpsw-0b(%r13),0xfd       # disable io interrupts
        ssch   0(%r11)                   # go
	bz     2f-0b(%r13)
	tsch   0(%r4)
	bct    %r8,1b-0b(%r13)           # do retry
        bnz    5f-0b(%r13)               # houston, we have a problem

2:      lr     %r2,%r12                  # call _wait4de with subchannel id
        lr     %r3,%r10                  # and irb address as parameters
        bas    %r14,_wait4de_32-0b(%r13) # wait until DE or error
        tm     9(%r10),0xff              # test channel status
        bz     3f-0b(%r13)
        bct    %r9,1b-0b(%r13)           # something went wrong, retry.
3:      tm     8(%r10),0xd2              # test device status
        bz     4f-0b(%r13)
        bct    %r9,1b-0b(%r13)           # something went wrong, retry.
        j      5f                        # retries failed -> panic

4:      lm     %r6,%r15,120(%r15)
        br     %r14

5:      lhi    %r2,ESSCH
        bas    %r4,_panik_32-0b(%r13)

################################################################################
# Wait for interrupt subroutine
#  -r2: device subchannel id
#  -r3: address of irb
################################################################################

_wait4de_32:
        stm    %r6,%r15,24(%r15)
        basr   %r13,0                   # base register
0:      ahi    %r15,-96                 # create stack frame
        lr     %r1,%r2
        mvc    0x78(8),6f-0b(%r13)      # set i/o new psw
1:      lpsw   5f-0b(%r13)
2:      stnsm  .Lpsw-0b(%r13),0xfd      # disable io interrupts
        c      %r1,0xb8                 # compare subchannel id
        bne    1b-0b(%r13)              # unequal -> continue waiting
        tsch   0(%r3)
        tm     9(%r3),0xff              # test channel status
        bnz    4f-0b(%r13)

3:      tm     8(%r3),0x02              # got unit check ?
        bnz    4f-0b(%r13)

        tm     8(%r3),0x04              # got device end ?
        bz     1b-0b(%r13)              # still busy -> continue waiting

4:      lm     %r6,%r15,120(%r15)
        br     %r14
        .align 8
5:      .long  0x020a0000,0x80000000+1b
6:      .long  0x00080000,0x80000000+2b # io new psw
.Lpsw:  .long  0x0

################################################################################
# Panik routine. Loads a disabled wait psw or performs reipl if required.
#   -r2 : panik code
################################################################################

_panik_32:
	stm    %r6,%r15,24(%r15)
	basr   %r13,0                   # base register
0:	ahi    %r15,-96
	st     %r2,1f-0b+4(%r13)        # store code in address part of psw
	bras   %r14,_print_exit_message

	# perform reipl: check lowcore for the address of an IPL Information
	# Block followed by a valid checksum (as defined in lowcore.h and set
	# by ipl.c). In case of match use diag308 to IPL.

	l     %r2,__LC_IPIB(%r0)                 # IPL Information Block
	ltr   %r2,%r2                            # re-ipl required?
	bz    .Lnoreipl-0b(%r13)                 # no, skip diag308
	l     %r3,0(%r2)                         # length of IPIB
	sr    %r1,%r1                            # zero accumator
.Lcksmloop:
	cksm  %r1,%r2                            # compute IPIB checksum
	bnz   .Lcksmloop-0b(%r13)                # repeat if not complete
	cl    %r1,__LC_IPIB+4(%r0)               # compare with cksm in lowcore
	bne   .Lnoreipl-0b(%r13)                 # no match, skip diag308
	l     %r2,__LC_IPIB(%r0)                 # reload IPL Information Block
	lhi   %r4,DIAG308_SET                    # load subcode
	diag  %r2,%r4,0x308                      # set IPL parameters
	lhi   %r4,DIAG308_IPL                    # load subcode
	diag  %r2,%r4,0x308                      # perform load clear
.Lnoreipl:
	lpsw   1f-0b(%r13)
	.align 8
1:	.long  0x000a0000,0x00000000

.endm /* dump_io_subroutines_32 */


################################################################################
# MACRO: dump_common_fn_32
# - _take_dump_32
# - _count_mem_32
# - _wait4de_32
# - _store_status_32
# - _copy_lowcore_32
################################################################################

.macro dump_common_fn_32
.align 2

################################################################################
# Take the dump
#  - no parameters
################################################################################

_take_dump_32:
        stm   %r6,%r15,24(%r15)
        basr  %r13,0
.Lbase: ahi   %r15,-96                           # create stack frame

	sckc  .Lclock_comparator-.Lbase(%r13)    # set clock comparator to future
	spt   .Lcpu_timer-.Lbase(%r13)           # set cpu timer to future

        lhi    %r6,ZERO_MEM_START                # clear memory
        lhi    %r7,ZERO_MEM_START
        ahi    %r7,ZERO_MEM_SIZE
        sr     %r7,%r6
        sr     %r8,%r8
        sr     %r9,%r9
        mvcle  %r6,%r8,0
        jo     .-4

        bas   %r14,_store_status_32-.Lbase(%r13) # store status

        # initialize sclp

        lhi    %r2,0
        bras   %r14,_sclp_setup

        # print start message

        la     %r2,.Lmsg_start-.Lbase(%r13)
        bras   %r14,_sclp_print

        # print OS type

        la     %r2,.Lmsg_waring_os-.Lbase(%r13)
        l      %r10,.Ldh_arch-.Lbase(%r13)
        chi    %r10,ARCH_S390X_ID
        bz     .Larch64-.Lbase(%r13)
        la     %r2,.Lmsg_31bit_os-.Lbase(%r13)
.Larch64:
        bras   %r14,_sclp_print

        # count memory

        bas   %r14,_count_mem_32-.Lbase(%r13)

        # copy dump header

        stck    .Ldh_time-.Lbase(%r13)            # store time
        stidp   .Ldh_cpuid-.Lbase(%r13)           # store CPU ID

        lhi     %r3,HEADER_PAGE_START
        mvc     0(256,%r3),.Ldh_dumpheader-.Lbase(%r13)

        # dump memory

        l     %r14,.Ldump_mem_32-.Lbase(%r13)
        basr  %r14,%r14

        # exit

        la    %r2,OK
        bas   %r14,_panik_32-.Lbase(%r13)        # everything ok: stop now
.align 8
.Lclock_comparator:
	.quad  0xffffffffffffffff
.Lcpu_timer:
	.quad  0x7fffffffffffffff

################################################################################
# Find out memory size:
# Use Read SCP or Read SCP forced to do this
#  - no parameters
################################################################################

_count_mem_32:
        stm    %r6,%r15,24(%r15)
        basr   %r13,0                         # base register
0:      ahi    %r15,-96                       # create stack frame

        bras  %r14,_sclp_read_info
        chi   %r2,0
        bne   .Lsclp_read_info_ok-0b(%r13)
        lhi   %r2,EMEMCOUNT
        bras  %r14,_panik_32
.Lsclp_read_info_ok:
        lr    %r12,%r2
.Lprocsccb:
        lhi   %r1,0
        icm   %r1,3,SCPINCR1_OFF(%r12)  # use this one if != 0
        jnz   .Lscnd
        lhi   %r1,0x800                 # otherwise report 2GB
.Lscnd:
        lhi   %r3,0x800                 # limit reported memory to 2GB
        cr    %r1,%r3
        jl    .Lno2gb
        lr    %r1,%r3
.Lno2gb:
        xr    %r3,%r3                   # same logic
        ic    %r3,SCPA1_OFF(%r12)
        chi   %r3,0x00
        jne   .Lcompmem
        l     %r3,SCPA2_OFF(%r12)
.Lcompmem:
        mr    %r2,%r1                   # mem in MB on 128-bit
        l     %r1,.Lonemb-0b(%r13)
        mr    %r2,%r1                   # mem size in bytes in %r3

        lr    %r12,%r3
        st    %r12,.Ldh_real_mem_size+4-0b(%r13)
        l     %r7,.Lmem_upper_limit_addr-0b(%r13)
        l     %r6,0(%r7)                     # check if we have an upper limit
        clr   %r12,%r6
        bl    .Lsavemem-0b(%r13)
        lr    %r12,%r6                          # upper mem limit set -> use it!
        la    %r2,.Lmsg_mem_limit_set-0b(%r13)  # print mem limit warning
        bras  %r14,_sclp_print
.Lsavemem:
        st    %r12,.Ldh_mem_size+4-0b(%r13)     # store memory size
        st    %r12,.Ldh_mem_end+4-0b(%r13)      # store memory end
        srl   %r12,12                           # calculate page count (/ 4096)
        st    %r12,.Ldh_num_pages-0b(%r13)      # store page count

        lm    %r6,%r15,120(%r15)
        br    %r14
.Lonemb:
        .int 0x100000
.Lmem_upper_limit_addr:
	.long	.Lmem_upper_limit+4


# expand Macros

   dump_common_store_status_32

# data 

.align 4
.Ldump_mem_32:       .long _dump_mem_32      # address of function

.endm    /* dump_common_fn_32 */


################################################################################
# MACRO: dump_idal_32
# - _create_ida_list_32
################################################################################

.macro dump_idal_32
.align 2
################################################################################
# This function creates a indirect data addressing list
# Parameters:
#  -r2:  start address
#  -r3:  length
#  -r4:  address of ida list
################################################################################
_create_ida_list_32:
        stm    %r6,%r15,24(%r15)
        basr   %r13,0                         # base register
0:      ahi    %r15,-96                       # create stack frame

        # setup new pgc psw for finding invalid pages

        mvc     .Ltmp_data-0b(8,%r13),104(%r0)      # save old psw
        mvc     104(8,%r0),.Lpage_invalid_psw-0b(%r13) # setup pgm check new

        lr      %r14,%r4                      # copy address of ida list
        lr      %r11,%r2                      # copy start address
        lr      %r9,%r3                       # copy length
        ar      %r9,%r11                      # compute end address
1:
        l       %r7,0(%r11)                   # test page
        lr      %r7,%r11
        b       .Lpage_ok-0b(%r13)
.Lpage_invalid:
        lhi     %r7,ZERO_PAGE_START
.Lpage_ok:
        st      %r7,0(%r14)                   # store address
        ahi     %r14,4                        # update ida pointer
        ahi     %r11,2048                     # update data address
        clr     %r11,%r9                      # enough ?
        bl      1b-0b(%r13)                   # branch if r11 < r9

        mvc     104(8,%r0),.Ltmp_data-0b(%r13) # restore pgm check new
        lm      %r6,%r15,120(%r15)
        br      %r14                          # return to caller

.Lpage_invalid_psw:
        .long 0x00080000,0x80000000 + .Lpage_invalid

.endm /* dump_idal_32 */

#endif /* __s390x__ */




################################################################################
# MACRO: dump_common_store_status_32
# - _store_status_32
# - _copy_lowcore_32
################################################################################


.macro dump_common_store_status_32
.align 2

################################################################################
# store status of all cpus in their lowcores
#  - no parameters
################################################################################

_store_status_32:
        stm   %r6,%r15,24(%r15)
        basr  %r13,0                          # base register
0:      ahi   %r15,-96
        la    %r7,0x0                         # base register for 0 page

        ######## move lowcore info (assume user has made store  ########
        ######## status) to prefix-page                         ########

        bas   %r14,_copy_lowcore_32-0b(%r13)

        ######## stop all cpus and store status in prefix pages ########

        la    %r8,0                           # first cpu
        stap  .Lcurrent_cpu+2-0b(%r13)        # store current cpu address

1:
        cl    %r8,.Lcurrent_cpu-0b(%r13)      # is ipl cpu ?
        be    4f-0b(%r13)                     # if yes get next cpu
2:
        lr    %r9,%r7
        sigp  %r9,%r8,0x9                     # stop & store status of cpu
        bc   8,3f-0b(%r13)                    # accepted
        bc   4,4f-0b(%r13)                    # status stored: next cpu
        bc   2,2b-0b(%r13)                    # busy:          try again
        bc   1,4f-0b(%r13)                    # not op:        next cpu
3:
        bas   %r14,_copy_lowcore_32-0b(%r13)
4:
        la    %r8,1(%r8)                      # next cpu (r8 +=1)
        cl    %r8,.Llast_cpu-0b(%r13)         # is last possible cpu ?
        bl    1b-0b(%r13)                     # jump if not last cpu
.Lstore_status_exit:
        lm      %r6,%r15,120(%r15)
        br      %r14                          # return to caller
.Lcurrent_cpu:
        .long 0x0
.Llast_cpu:
        .long 0x0000ffff

################################################################################
# copy lowcore to prefix page
#  - no parameters
################################################################################

_copy_lowcore_32:
        stm    %r6,%r15,24(%r15)
        basr   %r13,0                         # base register
0:      ahi    %r15,-96

        l      %r3,0x108(%r0)                 # get prefix page from lowcore

        ###### check if lowcore address looks valid ######

        cl    %r3,.Llinux_start-0b(%r13)      # looks valid ?
        bl    .Lcpy_locore_exit-0b(%r13)      # if < linux-start addr
        l     %r6,.Lpage_align-0b(%r13)       # check page alignment
        nr    %r3,%r6
        cl    %r3,0x108(%r0)
        bnz   .Lcpy_locore_exit-0b(%r13)      # if not page aligned

        ###### copy lowcore                         ######

        lh    %r8,.Ldh_cpu_cnt-0b(%r13)       # Save lowcore pointer (32 bit)
        sll   %r8,2                           # in dump header
        lhi   %r9,PREFIX_ARR_START
        ar    %r9,%r8
        st    %r3,0(%r9)

        lh    %r8,__LC_CPU_ADDRESS(%r0)       # copy cpu address
        sth   %r8,__LC_CPU_ADDRESS(%r3)       # to lowcore

        # |-----------------------------------------------------------|
        # | Decimal |  Length   | Data                                |
        # | Address |  in Bytes |                                     |
        # |_________|___________|_____________________________________|
        # | 212     | 4         | Extended save area address          |
        # | 216     | 8         | CPU timer                           |
        # | 224     | 8         | Clock comparator                    |
        # | 256     | 8         | Current PSW                         |
        # | 264     | 4         | Prefix register                     |
        # | 288     | 64        | Access registers 0 through 15       |
        # | 352     | 32        | Floating-point registers 0 through 6|
        # | 384     | 64        | General registers 0 through 15      |
        # | 448     | 64        | Control registers 0 through 15      |
        # |_________|___________|_____________________________________|

        mvc   212(20,%r3),212(%r0)
        mvc   256(12,%r3),256(%r0)
        mvc   288(224,%r3),288(%r0)

        lh    %r8,.Ldh_cpu_cnt-0b(%r13)       # Increment (online) CPU count
        ahi   %r8,1
        sth   %r8,.Ldh_cpu_cnt-0b(%r13)
.Lcpy_locore_exit:
        lh    %r10,.Ldh_real_cpu_cnt-0b(%r13) # Increment real CPU count
        ahi   %r10,1
        sth   %r10,.Ldh_real_cpu_cnt-0b(%r13)

        lm    %r6,%r15,120(%r15)
        br    %r14                            # return to caller
.Lpage_align:
        .long -4096
.Llinux_start:
        .long  0x10000

.endm /* dump_common_store_status_32 */

.macro tmp_data
.align 8
.Ltmp_data:
.fill  64,1,0
.endm

.macro dump_messages
.align 2

################################################################################
# Print out an exit message to the screen depending on the value of register 2
# Parameters:
#  -r2: exit code
################################################################################

_print_exit_message:
	stm    %r6,%r15,24(%r15)
	basr   %r13,0                         # base register
0:	ahi    %r15,-96

	lr     %r10,%r2

	# ok

	la     %r2,.Lmsg_ok-0b(%r13)
	chi    %r10, OK
	be     .Lreturn_with_msg-0b(%r13)

	# got an error

	la     %r2,.Lmsg_err-0b(%r13)
	bras   %r14,_sclp_print

	# device too small

	la     %r2,.Lmsg_err_mem-0b(%r13)
	chi    %r10, EMEM
	be     .Lreturn_with_msg-0b(%r13)

	# device not supported

	la     %r2,.Lmsg_err_dev_inval-0b(%r13)
	chi    %r10, EDEV_INVAL
	be     .Lreturn_with_msg-0b(%r13)

	# multi-volume dumper already printed message

	chi    %r10, ENODEV
	bnl    .Lreturn-0b(%r13)

	# I/O Error

	lr     %r2,%r10
	la     %r3,.Lmsg_err_code-0b(%r13)
	bras   %r14,_hex_to_ebcdic
	la     %r2,.Lmsg_err_io-0b(%r13)

.Lreturn_with_msg:
	bras   %r14,_sclp_print

#if !defined(__s390x__)

	# check for arch warning (31 bit dumper, but 64 bit OS)

	l      %r10,.Ldh_arch-0b(%r13)
	chi    %r10,ARCH_S390X_ID
	bnz    .Larch_check_ok-0b(%r13)
	la     %r2,.Lmsg_warning_reg_set-0b(%r13)
	bras   %r14,_sclp_print
.Larch_check_ok:
#endif 
.Lreturn:
	lm    %r6,%r15,120(%r15)
	br    %r14                            # return to caller

# "Dump successful

.Lmsg_ok:
	.byte 0xc4, 0xa4, 0x94, 0x97, 0x40, 0xa2, 0xa4, 0x83
	.byte 0x83, 0x85, 0xa2, 0xa2, 0x86, 0xa4, 0x93, 0x00

# "Warning: truncated register set (use 64 bit dump tool)"

.Lmsg_warning_reg_set:
	.byte 0xe6, 0x81, 0x99, 0x95, 0x89, 0x95, 0x87, 0x7a
	.byte 0x40, 0xa3, 0x99, 0xa4, 0x95, 0x83, 0x81, 0xa3
	.byte 0x85, 0x84, 0x40, 0x99, 0x85, 0x87, 0x89, 0xa2
	.byte 0xa3, 0x85, 0x99, 0x40, 0xa2, 0x85, 0xa3, 0x40
	.byte 0x4d, 0xa4, 0xa2, 0x85, 0x40, 0xf6, 0xf4, 0x40
	.byte 0x82, 0x89, 0xa3, 0x40, 0x84, 0xa4, 0x94, 0x97
	.byte 0x40, 0xa3, 0x96, 0x96, 0x93, 0x5d, 0x00

# "Dump failed"

.Lmsg_err:
	.byte 0xc4, 0xa4, 0x94, 0x97, 0x40, 0x86, 0x81, 0x89
	.byte 0x93, 0x85, 0x84, 0x00

# "Device too small"

.Lmsg_err_mem:
	.byte 0xc4, 0x85, 0xa5, 0x89, 0x83, 0x85, 0x40, 0xa3
	.byte 0x96, 0x96, 0x40, 0xa2, 0x94, 0x81, 0x93, 0x93
	.byte 0x00

# "Device not supported"

.Lmsg_err_dev_inval:
	.byte 0xc4, 0x85, 0xa5, 0x89, 0x83, 0x85, 0x40, 0x95
	.byte 0x96, 0xa3, 0x40, 0xa2, 0xa4, 0x97, 0x97, 0x96
	.byte 0x99, 0xa3, 0x85, 0x84, 0x00

# "I/O Error: RC=00000000"

.Lmsg_err_io:
	.byte 0xc9, 0x61, 0xd6, 0x40, 0xc5, 0x99, 0x99, 0x96
	.byte 0x99, 0x7a, 0x40, 0xd9, 0xc3, 0x7e
.Lmsg_err_code:
	.byte 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	.byte 0x00

# "zIPL v0.0.0 dump tool (64 bit)"

.Lmsg_start:
	.byte 0xa9, 0xc9, 0xd7, 0xd3, 0x40, 0xa5
	.byte S390_TOOLS_VERSION_EBCDIC
	.byte 0x40
	.byte 0x84, 0xa4, 0x94, 0x97, 0x40, 0xa3, 0x96, 0x96
	.byte 0x93, 0x40, 0x4d
#if defined(__s390x__)
	.byte 0xf6, 0xf4
#else
	.byte 0xf3, 0xf1
#endif
	.byte 0x40, 0x82, 0x89
	.byte 0xa3, 0x5d, 0x00

# "Dumping 64 bit OS"

.Lmsg_64bit_os:
	.byte 0xc4, 0xa4, 0x94, 0x97, 0x89, 0x95, 0x87, 0x40
	.byte 0xf6, 0xf4, 0x40, 0x82, 0x89, 0xa3, 0x40, 0xd6
	.byte 0xe2, 0x00

# "Dumping 31 bit OS"

.Lmsg_31bit_os:
	.byte 0xc4, 0xa4, 0x94, 0x97, 0x89, 0x95, 0x87, 0x40
	.byte 0xf3, 0xf1, 0x40, 0x82, 0x89, 0xa3, 0x40, 0xd6
	.byte 0xe2, 0x00

# "Warning: 31 bit dump tool, but 64 bit OS"

.Lmsg_waring_os:
	.byte 0xe6, 0x81, 0x99, 0x95, 0x89, 0x95, 0x87, 0x7a
	.byte 0x40, 0xf3, 0xf1, 0x40, 0x82, 0x89, 0xa3, 0x40
	.byte 0x84, 0xa4, 0x94, 0x97, 0x40, 0xa3, 0x96, 0x96
	.byte 0x93, 0x6b, 0x40, 0x82, 0xa4, 0xa3, 0x40, 0xf6
	.byte 0xf4, 0x40, 0x82, 0x89, 0xa3, 0x40, 0xd6, 0xe2
	.byte 0x00

# INFO: Using memory limit

.Lmsg_mem_limit_set:
	.byte 0xc9, 0xd5, 0xc6, 0xd6, 0x7a
	.byte 0x40, 0xe4, 0xa2, 0x89, 0x95, 0x87, 0x40, 0x94
	.byte 0x85, 0x94, 0x96, 0x99, 0xa8, 0x40, 0x93, 0x89
	.byte 0x94, 0x89, 0xa3, 0x25, 0x00

# "00000000 / 00000000 MB"

.Lmsg_progress_mb: 
	.byte 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0
	.byte 0x40, 0x61, 0x40
.Lmsg_mem_size:
	.byte 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0
	.byte 0x40, 0xd4, 0xc2, 0x00
.align 8
.endm
