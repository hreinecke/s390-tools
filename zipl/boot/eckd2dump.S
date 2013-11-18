/*
 *  Dump boot loader for 3380/3390 DASDs
 *    Copyright IBM Corp. 2001, 2006.
 *    Author(s): Michael Holzheu  <holzheu@de.ibm.com>
 *               Holger Smolinski <Holger.Smolinski@de.ibm.com>
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

#define STAGE2_DESC 0x78
#define IPL_BS 0x2000
#define BLOCKS_PER_WRITE 64                    /* makes 256K with 4K blksize */

################################################################################
# Function entry point at 0x2000 (not used for dump) is called with C linkage
#   %r2-%r3: load descriptor
#   %r3    : device subchannel id
#   %r4    : load address
################################################################################

#if defined(__s390x__)
dump_magic:  .long 0x5a45434b, 0x44363402 # "ZECKD64", version 2
#else
dump_magic:  .long 0x5a45434b, 0x44333102 # "ZECKD31", version 2
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
        bas    %r14,_get_device_characteristics_64-0b(%r13)
        llgf   %r2,IPL_SC                   # load ipl device subchannel id
        llgf   %r14,.Lenable_device_64-0b(%r13)
        basr   %r14,%r14
        llgf   %r5,.Ldev_blk_size-0b(%r13)  # get blocksize
        stcm   %r5,3,.Lwrccw+2-0b(%r13)     # store blocksize into wr template
        stcm   %r5,3,.Llodata+14-0b(%r13)   # store blocksize into lodata
        llgf   %r14,.Ltake_dump_64-0b(%r13)
        basr   %r14,%r14
1:      .long  0x10000-128                  # end of stack

################################################################################
# Get the device characteristics:
#  - no parameters
#
# Get device characteristics from zipl parameters
# The following parameter format is expected 2x8 byte:
#
# struct eckd_dump_param {
#       uint32_t        start_blk;
#       uint32_t        end_blk;
#       uint16_t        blocksize;
#       uint8_t.        num_heads;
#       uint8_t.        bpt;
#       char            reserved[4];
# } __attribute((packed));
################################################################################

_get_device_characteristics_64:
        stmg   %r6,%r15,48(%r15)
        basr   %r13,0                   # base register
0:      aghi   %r15,-200                # create stack frame

        mvc    .Ldev_start_blk-0b(4,%r13),STAGE2_DESC(%r0)
        mvc    .Ldev_end_blk-0b(4,%r13),STAGE2_DESC+0x4(%r0)
        mvc    .Ldev_blk_size-0b+2(2,%r13),STAGE2_DESC+0x8(%r0)
        mvc    .Ldev_nr_hds-0b+3(1,%r13),STAGE2_DESC+0xa(%r0)
        mvc    .Ldev_bpt-0b+3(1,%r13),STAGE2_DESC+0xb(%r0)
        lmg    %r6,%r15,248(%r15)
        br     %r14

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

        # calculate bytes per write (blksize * blwr)

        llgf    %r11,.Ldev_blk_size-0b(%r13)
        mh      %r11,.Lblocks_per_write-0b(%r13)
        st      %r11,.Lbytes_per_write-0b(%r13)

# write header

.Lheaders:                                    # write dump headers
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

.Lmemory:                                     # write memory

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
        lghi    %r4,1                         # write endmaker with one block
        stck    .Ld_end_time-0b(%r13)         # store end time
        lgr     %r2,%r11                      # restore r2
        la      %r3,.Ld_endmarker-0b(%r13)    # address of dump end marker
        lghi    %r4,1                         # write 4k at a time
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
        clgr    %r11,%r12                     # End block < dev_end_blk ?
        bl      1f-0b(%r13)                   # no
        lghi    %r2,EMEM                      # if yes panik
        llgf    %r14,.Lpanik_64-0b(%r13)
        basr    %r14,%r14 
        
1:      la      %r12,.Ldeccw-0b(%r13)
        st      %r12,8+.Lorb-0b(%r13)         # store cpaddr to orb
        la      %r12,.Lwrccw-0b(%r13) 
        oi      1(%r12),0x40                  # set CC in wr template
        oi      1(%r12),0x04                  # enable indirect data addressing 
        la      %r10,.Lida_list-0b(%r13)      # pointer to ida list

        # setup new pgc psw for finding invalid pages

        mvc     .Lold_pgc_psw-0b(16,%r13),464(%r0)      # save old psw
        mvc     464(16,%r0),.Lpage_invalid_psw-0b(%r13) # setup pgm check new

        # first setup the write channel program

        lgr     %r11,%r4                      # save number of blocks

        llgf    %r6,.Ldev_bpt-0b(%r13)        # load bpt to r6
        llgf    %r7,.Ldev_nr_hds-0b(%r13)     # load heads to r7
        la      %r5,.Lwrloop-0b(%r13)
        br      %r5 
        
2:      # loop for number of block (nbl) time  

        # switch to next write CCW

        llgf    %r5,.Ldev_blk_size-0b(%r13)
        agr     %r3,%r5                       # add blksize to destination addr
        aghi    %r12,8                        # add sizeof(ccw) to base address
        aghi    %r10,8                        # next element in ida list
        mvc     0(8,%r12),.Lwrccw-0b(%r13)    # copy template to this ccw

.Lwrloop:       

        # build up next write CCW

        l       %r9,0(%r3)                    # test page
        lgr     %r9,%r3
        b       .Lpage_ok-0b(%r13) 
.Lpage_invalid:
	lghi    %r9,ZERO_PAGE_START

.Lpage_ok:
        stg     %r9,0(%r10)                   # store target addr to idalist

        st      %r10,4(%r12)                  # store ida addr to this ccw
        bct     %r4,2b-0b(%r13)               # decrement no of blks still to do
        ni      1(%r12),0x3f                  # no command chaining for last ccw

        # write CCWs are setup now               

        lgr     %r4,%r11                      # restore number of blocks
        stcm    %r4,3,.Llodata+2-0b(%r13)     # store number of blocks to lodata

        # compute end block 

        agr     %r4,%r2                       # r4: ebl = blk + nbl 
        bctr    %r4,0                         # decrement r4 (last blk touched)        
        
        # compute start track and start block on track

        srda    %r2,32                        # shift ==> 64 bit number 
        dr      %r2,%r6                       # trk = blk / bpt, bot = blk % bpt 
                                              # r3: trk, r2: bot
        la      %r2,1(%r2)                    # bot++ ( we start counting at 1 )
        stcm    %r2,1,.Llodata+12-0b(%r13)    # store bot to lodata

        # compute start cylinder and head

        xr      %r2,%r2                       # clear bot                  
        dr      %r2,%r7                       # cy=trk / heads, hd=trk % heads
                                              # r3: cy, r2: hd

        lgr     %r8,%r3                       # r8=upper 12 bits of cylinder
        srl     %r8,16                        # are coded into heads:
        sll     %r8,4                         # c0c1c2h0 (2 byte)

        sll     %r3,16                        # combine to CCHH in r3
        or      %r3,%r2
        or      %r3,%r8                       # add upper 12 bits
        st      %r3,.Ldedata+8-0b(%r13)       # store cchh to dedata
        st      %r3,.Llodata+4-0b(%r13)       # store cchh to lodata
        st      %r3,.Llodata+8-0b(%r13)       # store cchh to lodata

        # compute end track and end block on track

        lgr     %r11,%r5                      # save r5
        srda    %r4,32                        # shift ==> 64 bit number
        dr      %r4,%r6                       # tr2 = ebl / bpt
                                              # r5: tr2, r4: bot2
        # compute end cylinder and head

        xr      %r4,%r4                       # cy2=tr2/heads, hd2=hd2 % heads
        dr      %r4,%r7                       # r5: cy2, r4: hd2

        lgr     %r8,%r5                       # r8=upper 12 bits of cylinder
        srl     %r8,16                        # are coded into heads:
        sll     %r8,4                         # c0c1c2h0 (2 byte)
        or      %r4,%r8

        stcm    %r5,3,.Ldedata+12-0b(%r13)    # store cy2 to dedata
        stcm    %r4,3,.Ldedata+14-0b(%r13)    # store hd2 to dedata
        lgr     %r5,%r11                      # restore r5

        # CCWs are setup now, arent they?

        llgf    %r2,IPL_SC                    # subchannel id
        la      %r3,.Lorb-0b(%r13)
        la      %r4,.Ltmp_data-0b(%r13)       # irb
        la      %r5,10                        # initialize retries

        bas     %r14,_ssch_64-0b(%r13)        # start I/O

        mvc     464(16,%r0),.Lold_pgc_psw-0b(%r13) # restore pgm check new
        lmg     %r6,%r15,248(%r15)
        br      %r14                          # return to caller
.align 8
.Lorb:
        .long 0x0049504c,0x0082ff00
        .long 0x00000000,0x00000000
.Ldedata:
        .long 0x80c00000,0x00000000
        .long 0x00000000,0x00000000
.Llodata:
        .long 0x01800001,0x00000000
        .long 0x00000000,0x03000000
        .long 0x12345678,0x00000000
.align 8
.Lida_list:
        .long 0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0
        .long 0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0
        .long 0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0
        .long 0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0
        .long 0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0
        .long 0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0
        .long 0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0
        .long 0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0
        .long 0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0
        .long 0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0
        .long 0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0
        .long 0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0
        .long 0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0
        .long 0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0
        .long 0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0
        .long 0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0
.Lpage_invalid_psw:
        .long 0x00000001,0x80000000,0x00000000,0x00000000 + .Lpage_invalid
.Lold_pgc_psw:
        .long 0x0,0x0,0x0,0x0 

################################################################################
# expand Macros
################################################################################

	hex_to_ebcdic
	print_progress_64
        dump_common_store_status_32
        dump_common_fn_64
        dump_io_subroutines_64
        dump_header
	dump_messages
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

.Larch_31:
        bas    %r14,_get_device_characteristics_32-0b(%r13)
        l      %r11,IPL_SC                  # load ipl device subchannel id
        lr     %r2,%r11
        l      %r14,.Lenable_device_32-0b(%r13)
        basr   %r14,%r14
        l      %r5,.Ldev_blk_size-0b(%r13)  # get blocksize
        stcm   %r5,3,.Lwrccw+2-0b(%r13)     # store blocksize into wr template
        stcm   %r5,3,.Llodata+14-0b(%r13)   # store blocksize into lodata
        l      %r14,.Ltake_dump_32-0b(%r13)
        basr   %r14,%r14
1:      .long  0x10000-96                   # end of stack


################################################################################
# Get the device characteristics:
#  - no parameters
#
# Get device characteristics from zipl parameters
# The following parameter format is expected 2x8 byte:
#
# struct eckd_dump_param {
#       uint32_t        start_blk;
#       uint32_t        end_blk;
#       uint16_t        blocksize;
#       uint8_t.        num_heads;
#       uint8_t.        bpt;
#       char            reserved[4];
# } __attribute((packed));
################################################################################

_get_device_characteristics_32:
        stm    %r6,%r15,24(%r15)
        basr   %r13,0                   # base register
0:      s      %r15,.Lc96-0b(%r13)      # create stack frame
        mvc    .Ldev_start_blk-0b(4,%r13),STAGE2_DESC(%r0)
        mvc    .Ldev_end_blk-0b(4,%r13),STAGE2_DESC+4(%r0)
        mvc    .Ldev_blk_size-0b+2(2,%r13),STAGE2_DESC+8(%r0)
        mvc    .Ldev_nr_hds-0b+3(1,%r13),STAGE2_DESC+0xa(%r0)
        mvc    .Ldev_bpt-0b+3(1,%r13),STAGE2_DESC+0xb(%r0)
        lm     %r6,%r15,120(%r15)
        br     %r14

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

        # calculate bytes per write (blksize * blwr)

        l       %r11,.Ldev_blk_size-0b(%r13)
        mh      %r11,.Lblocks_per_write-0b(%r13)
        st      %r11,.Lbytes_per_write-0b(%r13)

# write header

.Lheaders:                                    # write dump headers
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

.Lmemory:                                     # write memory

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
        la      %r3,.Ld_endmarker-0b(%r13)    # address of dump end marker
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
        la      %r12,.Lwrccw-0b(%r13) 
        oi      1(%r12),0x40                  # set CC in wr template

	# setup new pgc psw for finding invalid pages

	mvc     .Lold_pgc_psw-0b(8,%r13),104(%r0)      # save old psw
	mvc     104(8,%r0),.Lpage_invalid_psw-0b(%r13) # setup pgm check new

        # first setup the write channel program

        lr      %r11,%r4                      # save number of blocks

        l       %r6,.Ldev_bpt-0b(%r13)        # load bpt to r6
        l       %r7,.Ldev_nr_hds-0b(%r13)     # load heads to r7
        la      %r5,.Lwrloop-0b(%r13)
        br      %r5 /* FIXME */
        
2:      # loop for number of block (nbl) time  

        # switch to next write CCW

        l       %r5,.Ldev_blk_size-0b(%r13)
        ar      %r3,%r5                       # add blksize to destination addr
        ah      %r12,.Lccw_size-0b(%r13)      # add sizeof(ccw) to base address
        mvc     0(8,%r12),.Lwrccw-0b(%r13)    # copy template to this ccw
	
.Lwrloop:       
	l       %r9,0(%r3)                    # test page
	lr      %r9,%r3
	b       .Lpage_ok-0b(%r13)
.Lpage_invalid:
	lhi     %r9,ZERO_PAGE_START
.Lpage_ok:
        # build up next write CCW

        st      %r9,4(%r12)                   # store target addr to this ccw
        bct     %r4,2b-0b(%r13)               # decrement no of blks still to do
        ni      1(%r12),0x3f                  # no command chaining for last ccw

        # write CCWs are setup now               

        lr      %r4,%r11                      # restore number of blocks
        stcm    %r4,3,.Llodata+2-0b(%r13)     # store number of blocks to lodata

        # compute end block 

        ar      %r4,%r2                       # r4: ebl = blk + nbl 
        bctr    %r4,0                         # decrement r4 (last blk touched)        
        
        # compute start track and start block on track

        srda    %r2,32                        # shift ==> 64 bit number 
        dr      %r2,%r6                       # trk = blk / bpt, bot = blk % bpt 
                                              # r3: trk, r2: bot
        la      %r2,1(%r2)                    # bot++ ( we start counting at 1 )
        stcm    %r2,1,.Llodata+12-0b(%r13)    # store bot to lodata

        # compute start cylinder and head

        xr      %r2,%r2                       # clear bot                  
        dr      %r2,%r7                       # cy=trk / heads, hd=trk % heads
                                              # r3: cy, r2: hd

        lr      %r8,%r3                       # r8=upper 12 bits of cylinder
        srl     %r8,16                        # are coded into heads:
        sll     %r8,4                         # c0c1c2h0 (2 byte)

        sll     %r3,16                        # combine to CCHH in r3
        or      %r3,%r2
        or      %r3,%r8                       # add upper 12 bits
        st      %r3,.Ldedata+8-0b(%r13)       # store cchh to dedata
        st      %r3,.Llodata+4-0b(%r13)       # store cchh to lodata
        st      %r3,.Llodata+8-0b(%r13)       # store cchh to lodata

        # compute end track and end block on track

        lr      %r11,%r5                      # save r5
        srda    %r4,32                        # shift ==> 64 bit number
        dr      %r4,%r6                       # tr2 = ebl / bpt
                                              # r5: tr2, r4: bot2
        # compute end cylinder and head

        xr      %r4,%r4                       # cy2=tr2/heads, hd2=hd2 % heads
        dr      %r4,%r7                       # r5: cy2, r4: hd2

        lr      %r8,%r5                       # r8=upper 12 bits of cylinder
        srl     %r8,16                        # are coded into heads:
        sll     %r8,4                         # c0c1c2h0 (2 byte)
        or      %r4,%r8

        stcm    %r5,3,.Ldedata+12-0b(%r13)    # store cy2 to dedata
        stcm    %r4,3,.Ldedata+14-0b(%r13)    # store hd2 to dedata
        lr      %r5,%r11                      # restore r5

        # CCWs are setup now, arent they?

        l       %r2,IPL_SC                    # subchannel id
        la      %r3,.Lorb-0b(%r13)
        la      %r4,.Ltmp_data-0b(%r13)       # irb
        la      %r5,10                        # initialize retries

        bas     %r14,_ssch_32-0b(%r13)        # start I/O

        mvc     104(8,%r0),.Lold_pgc_psw-0b(%r13) # restore pgm check new

        lm      %r6,%r15,120(%r15)
        br      %r14                          # return to caller
.Lccw_size:
        .word 0x8
.align 8
.Lorb:
        .long 0x0049504c,0x0080ff00           # intparm is " IPL"
        .long 0x00000000,0x00000000
.Ldedata:
        .long 0x80c00000,0x00000000
        .long 0x00000000,0x00000000
.Llodata:
        .long 0x01800001,0x00000000
        .long 0x00000000,0x03000000
        .long 0x12345678,0x00000000
.Lpage_invalid_psw:
	.long  0x00080000,0x80000000 + .Lpage_invalid
.Lold_pgc_psw:
        .long 0x0,0x0

################################################################################
# expand Macros
################################################################################

	hex_to_ebcdic
	print_progress_32
        dump_common_fn_32
        dump_io_subroutines_32
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
.Ldev_blk_size:
        .long  0x00000000
.Ldev_nr_hds:
        .long  0x00000000
.Ldev_bpt:
        .long  0x00000000
.Ldev_start_blk:
        .long  0x00000000
.Ldev_end_blk:
        .long  0x00000000



#
# CCWs
#

.align 8

# channel program for one write

.Ldeccw:
        .long 0x63400010,0x00000000+.Ldedata     # define extent
.Lloccw:
        .long 0x47400010,0x00000000+.Llodata     # locate record
.Lwrccw:
        .long 0x8d000000,0x00000000              # update Key & data

.org (IPL_BS - 8)
.Lmem_upper_limit:
        .long 0xffffffff,0xffffffff              # can be used for memsize=xxx
