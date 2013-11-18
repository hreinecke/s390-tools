/*
 *  Dump boot loader for FBA DASDs
 *    Copyright IBM Corp. 2004, 2006.
 *    Author(s): Michael Holzheu  <holzheu@de.ibm.com>
 *               
 * Uses extern functions:            
 *  - _panik
 *  - _enable_device
 *  - _take_dump
 *
 * Functions:
 *  - _dump_mem
 */

#include "dumpcommon.S"
#if defined(__s390x__)
#define USE_64BIT_SCLP
#endif
#include "sclp.S"

/* General defines */

#define IPL_BS 0x2000
#define BLOCKS_PER_WRITE 64 
#define FBA_BLK_SIZE 0x200
#define STAGE2_DESC 0x78

################################################################################
# Function entry point at 0x2000 (not used for dump) is called with C linkage
#   %r2-%r3: load descriptor
#   %r3    : device subchannel id
#   %r4    : load address
################################################################################

#if defined(__s390x__)
dump_magic:  .long 0x5a444642, 0x41363402 # ZDFBA64, version 2
#else
dump_magic:  .long 0x5a444642, 0x41333102 # ZDFBA31, version 2
#endif

#if defined(__s390x__)


/******************************** 64 BIT **************************************/


################################################################################
# Program execution of the second stage boot loader starts at 0x2008
################################################################################

        .globl _start
_start: 
        basr  %r13,0
.Linit_base:
        la    %r9,0
        st    %r9,.Ldh_arch-.Linit_base(%r13) # init arch 
        l     %r15,1f-.Linit_base(%r13)       # load end of stack address
        la    %r7,0
        tm    __LC_ARCH_MODE_ID(%r9),0x01     # check arch mode
        bnz   .Larch_64-.Linit_base(%r13)

        /* 32 bit store status */

        l     %r14,.Lstore_status_32-.Linit_base(%r13)
        basr  %r14,%r14
        la    %r10,ARCH_S390_ID
        st    %r10,.Ldh_arch-.Linit_base(%r13)

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

        llgf   %r14,.Lstore_status_64-0b(%r13)
        basr   %r14,%r14
        lghi   %r10,ARCH_S390X_ID
        st     %r10,.Ldh_arch-0b(%r13)

.Larch_32:

        # get start and end block

        mvc    .Ldev_start_blk-0b(4,%r13),STAGE2_DESC+4
        mvc    .Ldev_end_blk-0b(4,%r13),STAGE2_DESC+8+4

        llgf   %r2,IPL_SC                   # load ipl device subchannel id
        llgf   %r14,.Lenable_device_64-0b(%r13)
        basr   %r14,%r14
        llgf   %r14,.Ltake_dump_64-0b(%r13)
        basr   %r14,%r14
1:      .long  0x10000-128                  # end of stack

################################################################################
# Dump memory
#  -no parameters
################################################################################

_dump_mem_64:    
        stmg   %r6,%r15,48(%r15)
        basr   %r13,0                         # base register
0:      aghi   %r15,-200                      # create stack frame

        # init progress bar

        bras %r14,_init_print_progress_64

        # setup define extend

        l         %r7,.Ldev_end_blk-0b(%r13)
        st        %r7,12+.Ldedata-0b(%r13)

        # calculate bytes per write (blksize * blwr)

        llgf    %r11,.Ldev_blk_size-0b(%r13)
        mh      %r11,.Lblocks_per_write-0b(%r13)
        st      %r11,.Lbytes_per_write-0b(%r13)

# write header

        llgf    %r11,.Ldev_start_blk-0b(%r13) # start block

        lgr     %r2,%r11
        lghi    %r3,HEADER_PAGE_START
        lghi    %r4,HEADER_SIZE
        srda    %r4,32                        # shift ==> 64 bit number        
        llgf    %r6,.Ldev_blk_size-0b(%r13)   # get blocksize

        dr      %r4,%r6                       # nr of blocks for header = 
                                              # HEADER_SIZE / BLOCKSIZE = r5
        lgr     %r4,%r5
        lgr     %r12,%r5                      # save nr of blocks        
        bas     %r14,_writeblock_64-0b(%r13)  # write block to disk
        ar      %r11,%r12                     # update block counter

# write memory

        lghi    %r10,0                        # start at address 0

.Lmloop:        
        lghi    %r4,BLOCKS_PER_WRITE          # write so many blocks at a time
        lgr     %r2,%r11                      # restore r2
        lgr     %r3,%r10                      # restore r3
        bas     %r14,_writeblock_64-0b(%r13)  # write block to disk
        llgf    %r2,.Lbytes_per_write-0b(%r13)
        agr     %r10,%r2                      # update data address
        aghi    %r11,BLOCKS_PER_WRITE         # skip to next block

        lgr     %r2,%r10                      # print progress to console
        bras    %r14,_print_progress_64

        lg      %r3,.Ldh_mem_size-0b(%r13)    # get memsize
        clgr    %r10,%r3                      # enough ?
        bl      .Lmloop-0b(%r13)              # branch if r10 < r3

# write end marker

.lendmarker:                                  # terminate dump file
        stck    .Ld_end_time-0b(%r13)         # store end time
        lgr     %r2,%r11                      # restore r2
	lghi     %r3, TMP_PAGE_START
	mvc     0(256,%r3),.Ld_endmarker-0b(%r13) # move to 4k boundary
        lghi    %r4,1                         # write one block
        bas     %r14,_writeblock_64-0b(%r13)  # write block to disk

        lmg     %r6,%r15,248(%r15)
        br      %r14                          # return to caller
.Lbytes_per_write:  .long 0x00000000
.Lblocks_per_write: .word BLOCKS_PER_WRITE

################################################################################
# This function writes a block number given in r2 to disk
#  -r2:  number of first block to write ( input by caller )
#        We start counting with Block Nr 0 !!!
#  -r3:  address to write data from ( input by caller )
#  -r4:  number of blocks to write ( input by caller )
################################################################################

_writeblock_64:
        stmg    %r6,%r15,48(%r15)
        basr    %r13,0                        # base register
0:      aghi    %r15,-200                     # create stack frame

        # check if blocks are within range:

        lgr     %r11,%r2
        agr     %r11,%r4                      # End block
        llgf    %r12,.Ldev_end_blk-0b(%r13)
        clr     %r11,%r12                     # End block < dev_end_blk ?
        bl      1f-0b(%r13)                   # no
        lghi    %r2,EMEM                      # if yes panik
        llgf    %r14,.Lpanik_64-0b(%r13)
        basr    %r14,%r14 
        
1:      la      %r12,.Ldeccw-0b(%r13)
        st      %r12,8+.Lorb-0b(%r13)         # store cpaddr to orb
        la      %r12,.Lwrccw-0b(%r13)
        oi      1(%r12),0x04                  # enable indirect data addressing

        #setup locate ccw
        sth     %r4,2+.Llodata-0b(%r13)       # store number of blocks
        st      %r2,4+.Llodata-0b(%r13)       # store first block

        # setup write ccw
        la      %r12,.Lwrccw-0b(%r13)
        la      %r10,.Lida_list-0b(%r13)      # pointer to ida list
        st      %r10,4+.Lwrccw-0b(%r13)       # store idal data address
        lgr     %r7,%r4
        mh      %r7,2+.Ldev_blk_size-0b(%r13)
        sth     %r7,2+.Lwrccw-0b(%r13)        # store byte count

	# setup idas

	lgr	%r2,%r3                       # start address
	lgr     %r3,%r7                       # byte count
	lgr     %r4,%r10                      # address of ida list
        bas     %r14,_create_ida_list_64-0b(%r13) # create ida list
	
        # CCWs are setup now, arent they?

        llgf    %r2,IPL_SC                    # subchannel id
        la      %r3,.Lorb-0b(%r13)
        la      %r4,.Lirb-0b(%r13)
        la      %r5,10                        # initialize retries

        bas     %r14,_ssch_64-0b(%r13)        # start I/O

        lmg     %r6,%r15,248(%r15)
        br      %r14                          # return to caller
.align 8
.Lorb:
        .long 0x0049504c,0x0082ff00
        .long 0x00000000,0x00000000
.Lirb:
        .long 0x00000000,0x00000000
        .long 0x00000000,0x00000000
        .long 0x00000000,0x00000000
        .long 0x00000000,0x00000000
        .long 0x00000000,0x00000000
        .long 0x00000000,0x00000000
        .long 0x00000000,0x00000000
        .long 0x00000000,0x00000000
.Ldedata:
        .long 0xc0000200,0x00000000
        .long 0x00000000,0x00001000
.Llodata:
        .long 0x01000000,0x00000000
.align 8
.Lida_list: # enough for 128 blocks a 512 byte
        .long 0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0
        .long 0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0
        .long 0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0
        .long 0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0

################################################################################
# expand Macros
################################################################################

        hex_to_ebcdic
        print_progress_64
        dump_common_store_status_32
        dump_common_fn_64
        dump_io_subroutines_64
        dump_idal_64
        dump_messages
        dump_header
        tmp_data
        sclp_base
        sclp_print
        sclp_read_info

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


################################################################################
# Program execution of the second stage boot loader starts at 0x2008
################################################################################

        .globl _start
_start: basr   %r13,0
0:      l      %r15,1f-0b(%r13)             # load end of stack address

	tm    __LC_ARCH_MODE_ID,0x01        # check arch mode
	bz   .Larch_31-0b(%r13)
	la    %r10,ARCH_S390X_ID
	st    %r10,.Ldh_arch-0b(%r13)

        # get start and end block

        mvc    .Ldev_start_blk-0b(4,%r13),STAGE2_DESC+4
        mvc    .Ldev_end_blk-0b(4,%r13),STAGE2_DESC+8+4

.Larch_31:
        l      %r11,IPL_SC                  # load ipl device subchannel id
        lr     %r2,%r11
        l      %r14,.Lenable_device_32-0b(%r13)
        basr   %r14,%r14
        l      %r14,.Ltake_dump_32-0b(%r13)
        basr   %r14,%r14
1:      .long  0x10000-96                   # end of stack


################################################################################
# Dump memory
#  - no parameters
################################################################################

_dump_mem_32:    
        stm    %r6,%r15,24(%r15)
        basr   %r13,0                         # base register
0:      s      %r15,.Lc96-0b(%r13)            # create stack frame

        # init progress bar

        bras %r14,_init_print_progress_32

        # setup define extend

	l         %r7,.Ldev_end_blk-0b(%r13)
	st        %r7,12+.Ldedata-0b(%r13)

        # calculate bytes per write (blksize * blwr)

        l       %r11,.Ldev_blk_size-0b(%r13)
        mh      %r11,.Lblocks_per_write-0b(%r13)
        st      %r11,.Lbytes_per_write-0b(%r13)

# write header
        l       %r11,.Ldev_start_blk-0b(%r13) # start block

        lr      %r2,%r11
        lhi     %r3,HEADER_PAGE_START
        lhi     %r4,HEADER_SIZE
        srda    %r4,32                        # shift ==> 64 bit number        
        l       %r6,.Ldev_blk_size-0b(%r13)   # get blocksize

        dr      %r4,%r6                       # nr of blocks for header = 
                                              # HEADER_SIZE / BLOCKSIZE = r5
        lr      %r4,%r5
        lr      %r12,%r5                      # save nr of blocks        
        bas     %r14,_writeblock_32-0b(%r13)  # write block to disk
        ar      %r11,%r12                     # update block counter

# write memory

        la      %r10,0                        # start at address 0

.Lmloop:        
        la      %r4,BLOCKS_PER_WRITE          # write so many blocks at a time
        lr      %r2,%r11                      # restore r2
        lr      %r3,%r10                      # restore r3
        bas     %r14,_writeblock_32-0b(%r13)  # write block to disk
        l       %r2,.Lbytes_per_write-0b(%r13)
        ar      %r10,%r2                      # update data address
        ah      %r11,.Lblocks_per_write-0b(%r13) # skip to next block

        lr      %r2,%r10                      # print progress to console
        bras    %r14,_print_progress_32

        l       %r3,.Ldh_mem_size+4-0b(%r13)  # get memsize
        clr     %r10,%r3                      # enough ?
        bl      .Lmloop-0b(%r13)              # branch if r10 < r3

# write end marker

.lendmarker:                                  # terminate dump file
        la      %r4,1                         # write endmaker with one block
        stck    .Ld_end_time-0b(%r13)         # store end time
        lr      %r2,%r11                      # restore r2
	lhi     %r3, TMP_PAGE_START
        mvc     0(256,%r3),.Ld_endmarker-0b(%r13) # move to 4k boundary
        la      %r4,1                         # write 4k at a time
	bas     %r14,_writeblock_32-0b(%r13)  # write block to disk

        lm      %r6,%r15,120(%r15)
        br      %r14                          # return to caller
.Lbytes_per_write:  .long 0x00000000
.Lblocks_per_write: .word BLOCKS_PER_WRITE

################################################################################
# This function writes a block number given in r2 to disk
#  -r2:   number of first block to write ( input by caller )
#         We start counting with Block Nr 0 !!!
#  -r3:   address to write data from ( input by caller )
#  -r4:   number of blocks to write ( input by caller )
################################################################################

_writeblock_32:
        stm     %r6,%r15,24(%r15)
        basr    %r13,0                        # base register
0:      s       %r15,.Lc96-0b(%r13)           # create stack frame

        # check if blocks are within range:
        
        lr      %r11,%r2
        ar      %r11,%r4                      # End block
        l       %r12,.Ldev_end_blk-0b(%r13)
        clr     %r11,%r12                     # End block < dev_end_blk ?
        bl      1f-0b(%r13)                   # no
        lhi     %r2,EMEM                      # if yes panik
        l       %r14,.Lpanik_32-0b(%r13)
        basr    %r14,%r14 
        
1:      la      %r12,.Ldeccw-0b(%r13)
        st      %r12,8+.Lorb-0b(%r13)         # store cpaddr to orb

	#setup locate ccw
	sth     %r4,2+.Llodata-0b(%r13)       # store number of blocks
        st      %r2,4+.Llodata-0b(%r13)       # store first block

	# setup write ccw
        la      %r12,.Lwrccw-0b(%r13) 
	oi      1(%r12),0x04                  # enable ida
	la      %r10,.Lida_list-0b(%r13)      # pointer to ida list
	st      %r10,4+.Lwrccw-0b(%r13)       # store data address
	lr      %r7,%r4
	mh      %r7,2+.Ldev_blk_size-0b(%r13)
	sth     %r7,2+.Lwrccw-0b(%r13)        # store byte count 

	# setup idas
	lr      %r2,%r3                       # start address
	lr      %r3,%r7                       # byte count
        lr      %r4,%r10                      # address of ida list
        bas     %r14,_create_ida_list_32-0b(%r13)  # create the list
	
        # CCWs are setup now, arent they?

        l       %r2,IPL_SC                    # subchannel id
        la      %r3,.Lorb-0b(%r13)
        la      %r4,.Lirb-0b(%r13)
        la      %r5,10                        # initialize retries

        bas     %r14,_ssch_32-0b(%r13)        # start I/O

        lm      %r6,%r15,120(%r15)
        br      %r14                          # return to caller
.Lccw_size:
        .word 0x8
.align 8
.Lorb:
        .long 0x0049504c,0x0080ff00           # intparm is " IPL"
        .long 0x00000000,0x00000000
.Lirb:
        .long 0x00000000,0x00000000
        .long 0x00000000,0x00000000
        .long 0x00000000,0x00000000
        .long 0x00000000,0x00000000
        .long 0x00000000,0x00000000
        .long 0x00000000,0x00000000
        .long 0x00000000,0x00000000
        .long 0x00000000,0x00000000
.Ldedata:
        .long 0xc0000200,0x00000000
        .long 0x00000000,0x00001000
.Llodata:
        .long 0x01000000,0x00000000
.align 8
.Lida_list: # enough for 128 blocks a 512 byte
        .long 0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0
        .long 0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0
        .long 0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0
        .long 0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0

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
        tmp_data
        sclp_base
        sclp_print
        sclp_read_info

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

.Lc96:  .long  96 # for creating stackframes

# device characteristics
.align 8
.Ldev_start_blk:
        .long  0x00000000
.Ldev_end_blk:
        .long  0x00000000
.Ldev_blk_size:
        .long  FBA_BLK_SIZE 

#
# CCWs
#

.align 8

# channel program for one write

.Ldeccw:
        .long 0x63400010,0x00000000+.Ldedata     # define extent
.Lloccw:
        .long 0x43400008,0x00000000+.Llodata     # locate
.Lwrccw:
        .long 0x41200000,0x00000000              # write blocks

.org (IPL_BS - 8)
.Lmem_upper_limit:
        .long 0xffffffff,0xffffffff              # can be used for memsize=xxx
