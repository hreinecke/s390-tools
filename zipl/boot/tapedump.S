/*
 *  Dump boot loader for 3480/3490 tape devices
 *    Copyright IBM Corp. 1999, 2006.
 *    Author(s): Michael Holzheu (holzheu@de.ibm.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 * Uses extern functions:
 *  - _panik
 *  - _enable_device
 *  - _take_dump
 *  - _ssch
 *  - _wait4de
 *
 * Functions:
 *  - _dump_mem
 */

#include "dumpcommon.S"
#if defined(__s390x__)
#define USE_64BIT_SCLP
#endif
#include "sclp.S"

#define IPL_BS           0x2000
#define BLOCK_SIZE       0x8000 /* 32 KB */
#define DUMP_TOOL_START  0x2000 /* tool is loaded to this address in order */
                                /* not to overwrite page 0 */
/* Tape display messages */

#ifdef LOWER_CASE 
#define DISP_DUMP_END   0x2084a494,0x975c8595,0x84000000 /* dump*end */
#else
#define DISP_DUMP_END   0x20c4e4d4,0xd75cc5d5,0xc4000000 /* DUMP*END */
#endif

#define DISP_COUNT_SHIFT 24 /* all 16 MB display is changed */

/* Error codes */

#define ETAPE_REWUNL_1  0x00000001
#define ETAPE_REWUNL_2  0x00000002
#define ETAPE_WRITE     0x0003

/* Tape types */

#define TAPE_UNKNOWN 1
#define TAPE_3590    2
#define TAPE_3480    3 
#define TAPE_3490    4

################################################################################
# The first 24 bytes are loaded by ipl to addresses 0-23. (a PSW and two CCWs)
################################################################################

        # psw  
        .long  0x00080000,0x80000000+_start
        # rewind ccw
        .long  0x07000000,0x60000001
        # ccw to load dump utility to 0x2000
        .long  0x02000000+DUMP_TOOL_START ,0x20000000+IPL_BS


#if defined(__s390x__)

/******************************** 64 BIT **************************************/ 

.globl _start
_start: 
        basr  %r13,0
.Linit_base:
        xc    .Ldh_arch-.Linit_base(4,%r13),.Ldh_arch-.Linit_base(%r13)
        l     %r15,1f-.Linit_base(%r13)       # load end of stack address
        tm    __LC_ARCH_MODE_ID(%r0),0x01     # check arch mode
        bnz   .Larch_64-.Linit_base(%r13)

        /* 32 bit store status */

        larl  %r14,.Lstore_status_32
        basr  %r14,%r14
	mvi   .Ldh_arch+3-.Linit_base(%r13),ARCH_S390_ID
.Larch_64:
        la    %r7,2                             # first try code 2:
        la    %r6,0                             # 64 bit psws are restored
        sigp  %r7,%r6,0x12                      # switch to 64 bit
        bc    8,.Lswitched_64-.Linit_base(%r13) # order accepted ?
        la    %r7,1                             # code 2 failed - try code 1
        sigp  %r7,%r6,0x12                      # switch to 64 bit
.Lswitched_64:
        sam64                                   # switch to 64 bit addr mode
        basr   %r13,0
0:      llgf   %r15,1f-0b(%r13)                 # load end of stack address

        llgf   %r10,.Ldh_arch-0b(%r13)
        cghi   %r10,ARCH_S390_ID
        be     .Larch_32-0b(%r13)

        /* 64 bit store status */

        larl   %r14,_store_status_64
        basr   %r14,%r14
        lghi   %r10,ARCH_S390X_ID
        st     %r10,.Ldh_arch-0b(%r13)
.Larch_32:
        llgf   %r2,IPL_SC                     # load ipl device subchannel id
        larl   %r14,_enable_device_64
        basr   %r14,%r14
        bas    %r14,_get_device_characteristics_64-0b(%r13)
        larl   %r14,_take_dump_64
        basr   %r14,%r14
1:      .long  0x10000-128                    # end of stack

################################################################################
# Dump memory
#  - no parameters
################################################################################

_dump_mem_64:
        stmg   %r6,%r15,48(%r15)
        basr   %r13,0                         # base register
0:      aghi   %r15,-200                      # create stack frame

#
# first write a tape mark
#
        bas   %r14,_tapemark_64-0b(%r13)

# init progress bar

        bras %r14,_init_print_progress_64

#
# write header
#
        lghi  %r2,HEADER_PAGE_START
        lghi  %r3,HEADER_SIZE                 # size of header
        lgr   %r4,%r3                         # blocksize 
        bas   %r14,_writer_64-0b(%r13)
        
#
# write real storage to tape
#       
        lg    %r10,.Ldh_mem_size-0b(%r13)     # length
        lgr   %r12,%r10                       # save mem size
        lghi  %r11,0                          # start

        lgr   %r2,%r11                        # start
        lgr   %r3,%r10                        # length
        llgf  %r4,.Lblock_size-0b(%r13)       # blocksize
        bas   %r14,_writer_64-0b(%r13)        # write page

        clgr  %r2,%r12
        bhe   1f-0b(%r13)

        # Cartridge full

        lghi  %r2,EMEM
        larl  %r14,_panik_64
        basr  %r14,%r14

1:      # All memory written

#
# write end marker
#
        stck  .Ld_end_time-0b(%r13)           # store end time
        lghi  %r2, TMP_PAGE_START
        mvc   0(256,%r2),.Ld_endmarker-0b(%r13) # move to 4k boundary
        lghi  %r3,END_MARKER_SIZE             # size of end marker
        lghi  %r4,END_MARKER_SIZE             # blocksize
        bas   %r14,_writer_64-0b(%r13)
#
# write another tape mark
#
        bas   %r14,_tapemark_64-0b(%r13)

        la    %r2,.Lend_text-0b(%r13)
        bas   %r14,_load_display_64-0b(%r13)

        lmg   %r6,%r15,248(%r15)
        br    %r14                            # return to caller
.Lblock_size:
        .long BLOCK_SIZE
.Lend_text:
        .long DISP_DUMP_END

################################################################################
# subroutine for writing to tape
# Parameters:   
#  -r2: start address
#  -r3: length
#  -r4: blocksize
################################################################################

_writer_64:        
        stmg  %r6,%r15,48(%r15)
        basr  %r13,0                           # base register
0:      aghi  %r15,-200                        # create stack frame

        lgr    %r10,%r2                        # save start address
        lgr    %r11,%r3                        # save length
        agr    %r11,%r2                        # end address
        lgr    %r12,%r4                        # save blocksize
        lgr    %r8,%r2                         # mb counter = start

	# setup idrc

        la    %r7,.Lccwwrite-0b(%r13)
        st    %r7,8+.Lorbwrite-0b(%r13)
        lh    %r7,.Luse_idrc-0b(%r13)
        chi   %r7,1
        bne   .Lnocomp-0b(%r13)
        la    %r7,.Lccwwrite_compressed-0b(%r13)
        st    %r7,8+.Lorbwrite-0b(%r13)

	# setup idas 

.Lnocomp:

        la    %r9,.Ltmp_data-0b(%r13)
        st    %r9,.Lccwwrite+4-0b(%r13)        # initialize CCW data addresses
        sth   %r12,.Lccwwrite+2-0b(%r13)       # setup blocksize
.Lldlp:
        lgr   %r2,%r10                         # start address
        lgr   %r3,%r12                         # size = blocksize
        la    %r4,.Ltmp_data-0b(%r13)
        bas   %r14,_create_ida_list_64-0b(%r13)

        llgf  %r2,IPL_SC                       # subchannel id
        la    %r3,.Lorbwrite-0b(%r13)          # address of orb
        la    %r4,.Lirb-0b(%r13)               # address of irb
        lghi  %r5,10                           # 10 retries
        bas   %r14,_ssch_64-0b(%r13)           # write chunk of PAGE_SIZE bytes


        la    %r4,.Lirb-0b(%r13)
        tm    8(%r4),0xd2                      # something went wrong ?
        bz    .check_ue-0b(%r13)

        /* fatal write error */

        llgf  %r2,IPL_SC
        la    %r3,.Lorbsense-0b(%r13)
        la    %r4,.Lirb-0b(%r13)
        lghi  %r5,1
        bas   %r14,_ssch_64-0b(%r13)

        /* build error code: first byte ERA, last byte our error code */

        llgc  %r2,.Ltmp_data+3-0b(%r13)      # get ERA
        sll   %r2,24                           # move it to first byte
        ahi   %r2,ETAPE_WRITE

        llgf  %r14,.Lpanik_64-0b(%r13)
        basr  %r14,%r14

.check_ue:

        tm    8(%r4),0x1                       # unit exception ?
        bz    1f-0b(%r13)

        # unit exception: We reached End of Tape

        la    %r2,0(%r12,%r10)                 # r2 contains next write addr
        b     3f-0b(%r13)                      # return

1:
        # write was ok:

        cgr   %r10,%r8
        bl    2f-0b(%r13)


        srlg  %r2,%r10,20                      # change display
        la    %r3,.Ldump_text+1-0b(%r13)
        bas   %r14,_hex_to_ebcdic_64-0b(%r13)
        la    %r2,.Ldump_text-0b(%r13)
        bas   %r14,_load_display_64-0b(%r13)

        lghi  %r2,1
        sll   %r2,DISP_COUNT_SHIFT
        agr   %r8,%r2                          # update mb counter (+16MB)
2:
        agr   %r10,%r12                        # add block size

        lgr   %r2,%r10                         # print progress to console
        bras  %r14,_print_progress_64

        clgr  %r10,%r11                        # enough ?
        bl    .Lldlp-0b(%r13)

        lgr   %r2,%r10                         # return value
3:
        lmg   %r6,%r15,248(%r15)
        br    %r14                             

        .align 8
.Lorbwrite:
        .long  0x00000000,0x0082ff00,.Lccwwrite
        .align 8
.Lorbsense:
        .long  0x00000000,0x0080ff00,.Lccwsense
        .align 8
.Lccwwrite_compressed: /* note that 3480 does not support IDRC */
	.long  0xdb400001,.Lmodsetbyte
.Lccwwrite:
        .long  0x01240000,0x00000000
.Ldump_text:
        .long  0x20000000,0x00000000,0x00000000
.Lmodsetbyte:
	.long  0x08000000
	.align 8
.Lccwsense:
        .long  0x04200020,.Ltmp_data
################################################################################
#  Translate binary hex to decimal ebcdic
#   -r2: value (bin)
#   -r3: outbuffer (ebcdic - decimal)
################################################################################

_hex_to_ebcdic_64:
        stmg  %r6,%r15,48(%r15)
        basr  %r13,0                           # base register
0:      aghi  %r15,-200                        # create stack frame
        cvd   %r2,.Lout_packed-0b(%r13)        # convert to packed decimal
        unpk  0(8,%r3),.Lout_packed+4-0b(4,%r13)
        oi    7(%r3),0xf0                      # remove signed flag
        lmg   %r6,%r15,248(%r15)
        br    %r14
.Lout_packed:
        .long 0x0,0x0

################################################################################
# subroutine for reading the device characteristics
################################################################################

_get_device_characteristics_64:
        stmg   %r6,%r15,48(%r15)
        basr   %r13,0                   # base register
0:      aghi   %r15,-200                # create stack frame
        llgf   %r2,IPL_SC
        la     %r3,.Lorb_read_cc-0b(%r13)
        la     %r4,.Lirb-0b(%r13)
        lghi   %r5,2
        bas    %r14,_ssch_64-0b(%r13)   # start I/O: Read device characteristic

        # find out device type

        lhi     %r6,TAPE_3480
	lhi     %r7,0                   # idrc off
        clc     .Ltmp_data+3-0b(2,%r13),.L3480-0b(%r13)
        be      1f-0b(%r13)

        lhi     %r6,TAPE_3490
	lhi     %r7,1                   # idrc on
        clc     .Ltmp_data+3-0b(2,%r13),.L3490-0b(%r13)
        be      1f-0b(%r13) 

        lhi     %r6,TAPE_3590
	lhi     %r7,1                   # idrc on
        clc     .Ltmp_data+3-0b(2,%r13),.L3590-0b(%r13)
        be      1f-0b(%r13)

	lhi     %r6,TAPE_UNKNOWN
	lhi     %r7,0                   # idrc off
1:
	sth     %r6,.Ltape_type-0b(%r13)
	sth     %r7,.Luse_idrc-0b(%r13)
	lmg	%r6,%r15,248(%r15)
        br	%r14

.align 8
.Lorb_read_cc:
        .long  0x00000000,0x0080ff00,.Lrdcccw
        .align 8
.Lrdcccw:
        .long 0x64000040,0x00000000+.Ltmp_data    # read device characteristics
.L3590:
        .word   0x3590
.L3480:
        .word   0x3480
.L3490:
        .word   0x3490

################################################################################
# subroutine for writing to tape display
#  -r2: start address of text
################################################################################

_load_display_64:
        stmg    %r6,%r15,48(%r15)
        basr    %r13,0                        # base register
0:      aghi    %r15,-200                     # create stack frame
        st    %r2,.Lccwload_disp+4-0b(%r13)   # store text field
        l     %r2,IPL_SC                      # subchannel id
        la    %r3,.Lorbload_disp-0b(%r13)     # address of orb
        la    %r4,.Lirb-0b(%r13)              # address of irb
        lghi  %r5,1                           # no retry
        bas   %r14,_ssch_64-0b(%r13)          # load the display
        lmg   %r6,%r15,248(%r15)
        br    %r14
.align 8
.Lorbload_disp:
        .long  0x00000000,0x0080ff00,.Lccwload_disp
        .align 8
.Lccwload_disp:
        .long  0x9f200011,0x00000000


################################################################################
# write tapemark
#  - no parameters
################################################################################

_tapemark_64:
        stmg  %r6,%r15,48(%r15)
        basr  %r13,0                           # base register
0:      aghi  %r15,-200                        # create stack frame

        llgf  %r2,IPL_SC                       # subchannel id
        la    %r3,.Lorbmark-0b(%r13)           # r12 = address of orb
        la    %r4,.Lirb-0b(%r13)               # r5 = address of irb
        la    %r5,10                           # retries
        bas   %r14,_ssch_64-0b(%r13)           # write a tape mark

        lmg   %r6,%r15,248(%r15)
        br    %r14                             # return to caller
        .align 8
.Lorbmark:
        .long  0x00000000,0x0080ff00,.Lccwmark
        .align 8
.Lccwmark:
        .long  0x1f200001,0x00000000

################################################################################
# expand Macros
################################################################################

	hex_to_ebcdic
	print_progress_64
        dump_common_store_status_32
        dump_common_fn_64
        dump_io_subroutines_64
	dump_idal_64
        dump_header
	dump_messages
	sclp_base
	sclp_print
	sclp_read_info
	tmp_data

# extern functions
 
.Lpanik_64:
        .long      _panik_64
.Lenable_device_64:
        .long      _enable_device_64
.Ltake_dump_64:
        .long      _take_dump_64
.Lstore_status_32:
        .long      _store_status_32
.Lstore_status_64:
        .long      _store_status_64

#else /* __s390x__ */
 


/******************************** 32 BIT **************************************/


.globl _start
_start: basr   %r13,0
0:      l      %r15,1f-0b(%r13)               # load end of stack address

	tm    __LC_ARCH_MODE_ID,0x01          # check arch mode
	bz   .Larch_31-0b(%r13)
	la    %r10,ARCH_S390X_ID
	st    %r10,.Ldh_arch-0b(%r13)

.Larch_31:
        l      %r11,IPL_SC                    # load ipl device subchannel id
        lr     %r2,%r11
        l      %r14,.Lenable_device_32-0b(%r13)
        basr   %r14,%r14
	bas    %r14,_get_device_characteristics_32-0b(%r13)
        l      %r14,.Ltake_dump_32-0b(%r13)
        basr   %r14,%r14
1:      .long  0x10000-96                     # end of stack

################################################################################
# Dump memory
################################################################################

_dump_mem_32:
        stm    %r6,%r15,24(%r15)
        basr   %r13,0                         # base register
0:      ahi    %r15,-96                       # create stack frame

#
# first write a tape mark
#
        bas   %r14,_tapemark_32-0b(%r13)

# init progress bar

        bras %r14,_init_print_progress_32

#
# write header
#
        lhi   %r2,HEADER_PAGE_START
        mvc   0(256,%r2),.Ldh_dumpheader-0b(%r13) # move to 4k boundary
        lhi   %r3,HEADER_SIZE                 # size of header
        lr    %r4,%r3                         # blocksize 
        bas   %r14,_writer_32-0b(%r13)
        
#
# write real storage to tape
#       
        l     %r10,.Ldh_mem_size+4-0b(%r13)   # length
        lr    %r12,%r10                       # save mem size
        la    %r11,0                          # start

        lr    %r2,%r11                        # start
        lr    %r3,%r10                        # length
        l     %r4,.Lblock_size-0b(%r13)       # blocksize
        bas   %r14,_writer_32-0b(%r13)        # write page

        clr   %r2,%r12
        bhe   1f-0b(%r13)

        # Cartridge full

        lhi   %r2,EMEM
        l     %r14,.Lpanik_32-0b(%r13)
        basr  %r14,%r14

1:      # All memory written
#
# write end marker
#
        stck  .Ld_end_time-0b(%r13)           # store end time
        lhi   %r2, TMP_PAGE_START
        mvc   0(256,%r2),.Ld_endmarker-0b(%r13) # move to 4k boundary
        la    %r3,END_MARKER_SIZE             # size of end marker
        la    %r4,END_MARKER_SIZE             # blocksize
        bas   %r14,_writer_32-0b(%r13)
#
# write another tape mark
#
        bas   %r14,_tapemark_32-0b(%r13)

        la    %r2,.Lend_text-0b(%r13)
        bas   %r14,_load_display_32-0b(%r13)

        lm    %r6,%r15,120(%r15)
        br    %r14                            # return to caller
.Lblock_size:       
        .long BLOCK_SIZE
.Lend_text:
        .long DISP_DUMP_END

################################################################################# _init_tape
################################################################################

_init_tape_32:
        stm   %r6,%r15,24(%r15)
        basr  %r13,0                           # base register
0:      ahi   %r15,-96                         # create stack frame

        l     %r2,IPL_SC                       # subchannel id
        la    %r3,.Lorbmode_set-0b(%r13)
        la    %r4,.Lirb-0b(%r13)
        la    %r5,2
        bas    %r14,_ssch_32-0b(%r13)
        lm    %r6,%r15,120(%r15)
        br    %r14                             # return to caller
.align 8
.Lorbmode_set:
        .long  0x00000000,0x0080ff00,.Lccwmode_set
        .align 8
.Lccwmode_set:
        .long  0xdb200001,.Lmode_set
.Lmode_set:
        .long  0x00000000 /* buffered mode + IDRC */

################################################################################
# subroutine for writing to tape
# Parameters:   
#  -r2: start address
#  -r3: length
#  -r4: blocksize
#
# Returns:
#  -r2: up to this address we have written the memory
################################################################################

_writer_32:        
        stm   %r6,%r15,24(%r15)
        basr  %r13,0                           # base register
0:      ahi   %r15,-96                         # create stack frame

        lr    %r10,%r2                         # save start address
        lr    %r11,%r3                         # save length
        ar    %r11,%r2                         # end address
        lr    %r12,%r4                         # save blocksize 
        lr    %r8,%r2                          # mb counter = start

	# setup idrc

	la    %r7,.Lccwwrite-0b(%r13)
	st    %r7,8+.Lorbwrite-0b(%r13)
	lh    %r7,.Luse_idrc-0b(%r13)
	chi   %r7,1
	bne   .Lnocomp-0b(%r13)
	la    %r7,.Lccwwrite_compressed-0b(%r13)
        st    %r7,8+.Lorbwrite-0b(%r13)

.Lnocomp:

        la    %r9,.Ltmp_data-0b(%r13)
        st    %r9,.Lccwwrite+4-0b(%r13)        # initialize CCW data addresses
        sth   %r12,.Lccwwrite+2-0b(%r13)       # setup blocksize
.Lldlp:

        lr    %r2,%r10                         # start address
	lr    %r3,%r12                         # size = blocksize
	la    %r4,.Ltmp_data-0b(%r13)
        bas   %r14,_create_ida_list_32-0b(%r13)

        l     %r2,IPL_SC                       # subchannel id
        la    %r3,.Lorbwrite-0b(%r13)          # address of orb
        la    %r4,.Lirb-0b(%r13)               # address of irb
        la    %r5,10                           # 10 retries
        bas   %r14,_ssch_32-0b(%r13)           # write chunk of PAGE_SIZE bytes


        la    %r4,.Lirb-0b(%r13)
        tm    8(%r4),0xd2                      # something went wrong ?
        bz    .check_ue-0b(%r13)

        /* fatal write error */

        l     %r2,IPL_SC
        la    %r3,.Lorbsense-0b(%r13)
        la    %r4,.Lirb-0b(%r13)
        la    %r5,1
        bas   %r14,_ssch_32-0b(%r13)

        /* build error code: first byte ERA, last byte our error code */ 

        la    %r2,0 
        ic    %r2,.Ltmp_data+3-0b(%r13)      # get ERA
        sll   %r2,24                           # move it to first byte 
        ahi   %r2,ETAPE_WRITE
 
        l     %r14,.Lpanik_32-0b(%r13)
        basr  %r14,%r14

.check_ue:

        tm    8(%r4),0x1                       # unit exception ?
        bz    1f-0b(%r13)

        # unit exception: We reached End of Tape

        lr    %r2,%r10                         # r2 contains
        ar    %r2,%r12                         # next write addr 
        b     3f-0b(%r13)                      # return


1:       
        # write was ok:

        clr   %r10,%r8                         # one mb written ?
        bl    2f-0b(%r13)

         
        srl   %r2,20                           # change display
        la    %r3,.Ldump_text+1-0b(%r13) 
        bas   %r14,_hex_to_ebcdic_32-0b(%r13)
        la    %r2,.Ldump_text-0b(%r13)
        bas   %r14,_load_display_32-0b(%r13)

        la    %r2,1
        sll   %r2,DISP_COUNT_SHIFT
        ar    %r8,%r2                          # update mb counter (+16 MB)

2:
        ar    %r10,%r12                        # add block size

        lr    %r2,%r10                         # print progress to console
        bras  %r14,_print_progress_32

        clr   %r10,%r11                        # enough ?
        bl    .Lldlp-0b(%r13)
        lr    %r2,%r10                         # return value
3:
        lm    %r6,%r15,120(%r15)
        br    %r14                             
        .align 8
.Lorbwrite:
        .long  0x00000000,0x0080ff00,.Lccwwrite
        .align 8
.Lorbsense:
        .long  0x00000000,0x0080ff00,.Lccwsense
        .align 8
.Lccwwrite_compressed: /* note that 3480 does not support IDRC */ 
        .long  0xdb400001,.Lmodsetbyte
.Lccwwrite:
        .long  0x01240000,0x00000000
.Ldump_text:
        .long  0x20000000,0x00000000,0x00000000
.Lmodsetbyte:
        .long  0x08000000
.Lccwsense:
        .long  0x04200020,.Ltmp_data

################################################################################
#  Translate binary hex to decimal ebcdic
#   -r2: value (bin)
#   -r3: outbuffer (ebcdic - decimal)
################################################################################ 

_hex_to_ebcdic_32:
        stm  %r6,%r15,24(%r15)
        basr  %r13,0                           # base register
0:      ahi   %r15,-96                         # create stack frame
        cvd   %r2,.Lout_packed-0b(%r13)        # convert to packed decimal
        unpk  0(8,%r3),.Lout_packed+4-0b(4,%r13) 
        oi    7(%r3),0xf0                      # remove signed flag
        lm    %r6,%r15,120(%r15)
        br    %r14
.Lout_packed:
        .long 0x0,0x0

################################################################################# subroutine for reading the device characteristics
################################################################################ 
_get_device_characteristics_32:
	stm    %r6,%r15,24(%r15)
        basr   %r13,0                   # base register
0:      ahi    %r15,-96                 # create stack frame
        l      %r2,IPL_SC
        la     %r3,.Lorb_read_cc-0b(%r13)
        la     %r4,.Lirb-0b(%r13)
        la     %r5,2
        bas    %r14,_ssch_32-0b(%r13)   # start I/O: Read device characteristic
 
        # find out device type

	la      %r6,TAPE_3480
        la      %r7,0                   # idrc off
        clc     .Ltmp_data+3-0b(2,%r13),.L3480-0b(%r13)
        be      1f-0b(%r13) 

	la      %r6,TAPE_3490
        la      %r7,1                   # idrc on
        clc     .Ltmp_data+3-0b(2,%r13),.L3490-0b(%r13)
        be      1f-0b(%r13)

        la      %r6,TAPE_3590
	la      %r7,1                   # idrc on
        clc     .Ltmp_data+3-0b(2,%r13),.L3590-0b(%r13)
        be      1f-0b(%r13)
 
        la      %r6,TAPE_UNKNOWN
	la      %r7,0                   # idrc off
1:
        sth     %r6,.Ltape_type-0b(%r13)
	sth     %r7,.Luse_idrc-0b(%r13)
	lm      %r6,%r15,120(%r15)
        br      %r14
 
.align 8
.Lorb_read_cc:
        .long  0x00000000,0x0080ff00,.Lrdcccw
        .align 8
.Lrdcccw:
        .long 0x64000040,0x00000000+.Ltmp_data    # read device characteristics
.L3590:
        .word   0x3590
.L3480:
        .word   0x3480
.L3490:
        .word   0x3490

################################################################################
# subroutine for writing to tape display
#  -r2: start address of text
################################################################################

_load_display_32:
        stm    %r6,%r15,24(%r15)
        basr   %r13,0                         # base register
0:      ahi    %r15,-96                       # create stack frame
        st    %r2,.Lccwload_disp+4-0b(%r13)   # store text field
        l     %r2,IPL_SC                      # subchannel id
        la    %r3,.Lorbload_disp-0b(%r13)     # address of orb
        la    %r4,.Lirb-0b(%r13)              # address of irb
        la    %r5,1                           # no retry
        bas   %r14,_ssch_32-0b(%r13)          # load the display
 
        lm    %r6,%r15,120(%r15)
        br    %r14
.align 8
.Lorbload_disp:
        .long  0x00000000,0x0080ff00,.Lccwload_disp
        .align 8
.Lccwload_disp:
        .long  0x9f200011,0x00000000

################################################################################
# write tapemark
#  - no parameters
################################################################################

_tapemark_32:
        stm   %r6,%r15,24(%r15)
        basr  %r13,0                           # base register
0:      ahi   %r15,-96                         # create stack frame

        l     %r2,IPL_SC                       # subchannel id
        la    %r3,.Lorbmark-0b(%r13)           # r3 = address of orb
        la    %r4,.Lirb-0b(%r13)               # r4 = address of irb
        la    %r5,10                           # retries
        bas   %r14,_ssch_32-0b(%r13)           # write a tape mark

        lm    %r6,%r15,120(%r15)
        br    %r14                             # return to caller
        .align 8
.Lorbmark:
        .long  0x00000000,0x0080ff00,.Lccwmark
        .align 8
.Lccwmark:
        .long  0x1f200001,0x00000000

################################################################################
# expand Macros
################################################################################

	hex_to_ebcdic
	print_progress_32
	dump_common_fn_32
 	dump_io_subroutines_32
	dump_idal_32
	dump_messages
	dump_header
	sclp_base
	sclp_print
	sclp_read_info
	tmp_data

# extern functions
 
.Lpanik_32:
        .long      _panik_32
.Lenable_device_32:
        .long      _enable_device_32
.Ltake_dump_32:
        .long      _take_dump_32
     
#endif /* __s390x__ */

################################################################################
# DATA
################################################################################

.Ltape_type: .word 0 /* 3480/90 3590 etc. */
.Luse_idrc:  .word 1 /* 1 -> use idrc compression */
                     /* 0 -> do not use idrc (e.g. for 3480 not allowed) */

.Lirb:  .long  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
.org IPL_BS-8
.Lmem_upper_limit:
        .long 0xffffffff,0xffffffff              # can be used for memsize=xxx
