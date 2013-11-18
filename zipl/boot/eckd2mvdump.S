/*
 *  Multi-volume dump tool for ECKD DASDs
 *    Copyright IBM Corp. 2008
 *    Author(s): Michael Holzheu  <holzheu@de.ibm.com>
 *               Frank Munzert <munzert@de.ibm.com>
 *               Holger Smolinski <Holger.Smolinski@de.ibm.com>
 */

#include "dumpcommon.S"
#if defined(__s390x__)
#define USE_64BIT_SCLP
#include "sclp.S"

/* General defines */

#define MVDUMP_TOOL_SIZE 0x2000  /* length of dump tool without parmtable */
#define BLOCKS_PER_WRITE 64      /* makes 256K with 4Ki blksize           */
#define PTE_LENGTH 13            /* length of parameter table entry       */
#define MAGIC_BLOCK_OFFSET 3     /* dump tool starts on track 0, block 3  */

################################################################################
# Function entry point at 0x2000 (not used for dump) is called with C linkage
#   %r2-%r3: load descriptor
#   %r3    : device subchannel id
#   %r4    : load address
################################################################################

dump_magic:  .long 0x5a4d554c, 0x54363402     # "ZMULT64", version 2

/******************************** 64 BIT only**********************************/

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
	la    %r7,2                           # first try code 2:
	la    %r6,0                           # 64 bit psws are restored
	sigp  %r7,%r6,0x12                    # switch to 64 bit
	bc    8,.Lswitched_64-.Linit_base(%r13) # order accepted ?
	la    %r7,1                           # code 2 failed - try code 1
	sigp  %r7,%r6,0x12                    # switch to 64 bit
.Lswitched_64:
	sam64                                 # switch to 64 bit addr mode
	basr   %r13,0
0:      llgf   %r15,5f-0b(%r13)               # load end of stack address

	llgf   %r10,.Ldh_arch-0b(%r13)
	cghi   %r10,ARCH_S390_ID
	be     .Larch_32-0b(%r13)

        /* 64 bit store status */

	llgf   %r14,.Lstore_status_64-0b(%r13)
	basr   %r14,%r14
	lghi   %r10,ARCH_S390X_ID
	st     %r10,.Ldh_arch-0b(%r13)

.Larch_32:
	lghi   %r2,0                          # provide input register
	bras   %r14,_sclp_setup               # initialize sclp to write msg
	lhi    %r2,PARAM_START                # point to parameter table
	ahi    %r2,10                         # point to first table entry
	st     %r2,.Lparam_ptr-0b(%r13)       # initialize table entry pointer
	bas    %r14,_get_device_characteristics_64-0b(%r13)
	lhi    %r2,CCW_CHAIN_START            # point to 1st CCW in chain
	mvc    0(16,%r2),.Ldeccw-0b(%r13)     # move the first two CCWs
	mvi    16(%r2),0x86                   # move opcode into 1st write CCW
	llgf   %r14,.Ltake_dump_64-0b(%r13)
	basr   %r14,%r14
5:      .long  0x10000-128                    # end of stack

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

	# prepare dump header info
	mvi     .Ldh_mvdump-0b(%r13),0x01     # store mvdump indicator

.Lnextvol:
	mvi    .Ldev_full_flag-0b(%r13),0     # reset "device full" indicator
	# issue "Dumping to <devno>" status message
	la     %r2,.Lmsg_dump2devno-0b(%r13)
	bras   %r14,_sclp_print

	# calculate bytes per write (blksize * blwr)
	llgf    %r11,.Ldev_blk_size-0b(%r13)
	mh      %r11,.Lblocks_per_write-0b(%r13)
	st      %r11,.Lbytes_per_write-0b(%r13)

	# calculate number of blocks required to store header
	llgf    %r4,.Lheader_size-0b(%r13)
	srda    %r4,32                        # shift ==> 64 bit number
	llgf    %r6,.Ldev_blk_size-0b(%r13)   # get blocksize
	dr      %r4,%r6                       # nr of blocks for header =
                                              # HEADER_SIZE / BLOCKSIZE = r5

# Check whether parameter table on dump device has a valid time stamp.
# The parameter table is located right behind the dump tool, the corresponding
# block number is:  MAGIC_BLOCK_OFFSET + (MVDUMP_TOOL_SIZE / blocksize)

	mvi     .Llodata-0b(%r13),0x06        # indicate READ DATA operation
	mvi     .Ldedata-0b(%r13),0x40        # inhibit all write operations
	lghi    %r2,CCW_CHAIN_START           # point to 1st CCW in chain
	mvi     16(%r2),0x86                  # move read opcode into CCW
	lhi     %r2,MAGIC_BLOCK_OFFSET        # start block of dump tool
	lgr     %r10,%r5                      # mvdumptool size = 0x2000
	sll     %r10,1                        # (header size * 2)
	ar      %r2,%r10                      # start block of parameter table
	lghi    %r3,TMP_PAGE_START            # destination of read operation
	lghi    %r4,1                         # number of blocks to read
	bas     %r14,_ioblock_64-0b(%r13)     # read parameter table
	lghi    %r3,TMP_PAGE_START            # destination of read operation
	lghi    %r2,PARAM_START               # point to time stamp
	clc     0(8,%r3),0(%r2)               # found identical time stamp?
	be      .Lcheck_sign-0b(%r13)         # yes, check signature
	# issue "Inconsistent time stamps" message and exit
	la      %r2,.Lmsg_time_stamp-0b(%r13) # point to message
	bras    %r14,_sclp_print              # print message
	lghi    %r2,ENOTIME                   # inconsistent time stamps
	b      _panik_64-0b(%r13)             # --> exit

# Check whether dump partition has a valid dump signature.
# Bypass signature check if --force had been specified during zipl -M
# The dump signature is located at offset 512 relative to the partition start

.Lcheck_sign:
	larl    %r7,.Lforce
	tm      1(%r7),0x01                   # was zipl --force specified?
	bo      .Lheaders-0b(%r13)            # yes, skip signature check
	llgf    %r2,.Ldev_start_blk-0b(%r13)  # start block of partition
	lghi    %r3,TMP_PAGE_START            # destination of read operation
	lgr     %r4,%r5                       # number of blocks for header
	bas     %r14,_ioblock_64-0b(%r13)     # read start block
	lghi    %r3,TMP_PAGE_START            # destination of read operation
	clc     512(8,%r3),.Ldh_magic_number-0b(%r13) # did we find signature?
	be      .Lheaders-0b(%r13)            # yes, write data to partition
	# issue "Wrong signature" message and exit
	la      %r2,.Lmsg_signature-0b(%r13)  # point to message
	bras    %r14,_sclp_print              # print message
	lghi    %r2,ENOSIGN                   # invalid signature
	b      _panik_64-0b(%r13)             # --> exit

# write header

.Lheaders:                                    # write dump headers
	mvi     .Llodata-0b(%r13),0x01        # indicate WRITE DATA operation
	mvi     .Ldedata-0b(%r13),0x80        # permit update write operations
	lghi    %r2,CCW_CHAIN_START           # point to 1st CCW in chain
	mvi     16(%r2),0x8d                  # move write opcode into CCW

	llgf    %r11,.Ldev_start_blk-0b(%r13) # start block

	lgr     %r2,%r11
	lghi    %r3,HEADER_PAGE_START
	mvc     512(8,%r3),.Ldh_magic_number-0b(%r13) # preserve signature
	lgr     %r4,%r5
	lgr     %r12,%r5                      # save nr of blocks
	bas     %r14,_ioblock_64-0b(%r13)     # write block to disk
	ar      %r11,%r12                     # update block counter

# write memory

.Lmemory:                                     # write memory
	lg      %r10,.Lmem_current-0b(%r13)   # current memory address
.Lmloop:
	cli     .Ldev_full_flag-0b(%r13),1    # no more space left on device?
	bne     .Lmloop3-0b(%r13)             # no, just write to this one
	stg     %r10,.Lmem_current-0b(%r13)   # yes, remember memory address
	l       %r12,.Ldh_vol_nr-0b(%r13)     # get current volume number
	ahi     %r12,1                        # increment volume number
	st      %r12,.Ldh_vol_nr-0b(%r13)     # store next volume number
	lghi    %r3,HEADER_PAGE_START
	mvc     0(256,%r3),.Ldh_dumpheader-0b(%r13)
	lhi     %r11,PARAM_START              #
	ch      %r12,8(%r11)                  # last dump target?
	bl      .Lmloop2-0b(%r13)             # no, initialize next target
	lghi    %r2,EMEM                      # yes, all targets full
	llgf    %r14,.Lpanik_64-0b(%r13)      #
	basr    %r14,%r14
.Lmloop2:
	llgf    %r2,.Lparam_ptr-0b(%r13)      # yes, load parameter pointer
	la      %r2,PTE_LENGTH(%r2)           # increment parm table pointer
	st      %r2,.Lparam_ptr-0b(%r13)      # ... and store it back
	bas     %r14,_get_device_characteristics_64-0b(%r13)
	b       .Lnextvol-0b(%r13)            # write to next device
.Lmloop3:
	lghi    %r4,BLOCKS_PER_WRITE          # write so many blocks at a time
	llgf    %r9,.Lbytes_per_write-0b(%r13)
	lgr     %r2,%r11                      # load current block pointer
	agr     %r2,%r4                       # increment block pointer
	llgf    %r12,.Ldev_end_blk-0b(%r13)   # last block on dump target
	clgr    %r2,%r12                      # block pointer < dev_end_blk ?
	bl      .Lmloop4-0b(%r13)             # yes, write BLOCKS_PER_WRITE
	mvi     .Ldev_full_flag-0b(%r13),1    # no, set "device is full"
	lgr     %r4,%r12                      #
	sgr     %r4,%r11                      #
	aghi    %r4,1                         # number of remaining blocks
	llgf    %r9,.Ldev_blk_size-0b(%r13)   #
	mr      %r8,%r4                       #
	lgr     %r3,%r9                       # number of remaining bytes
	srl     %r9,12                        #
	sll     %r9,12                        # enforce multiple of 4k
	ltr     %r9,%r9                       # remaining bytes were less 4k?
	bz      .Lmloop-0b(%r13)              # yes, don't write to this volume
	sr      %r3,%r9                       # r3: remainder modulo 4k
	lghi    %r2,0
	d       %r2,.Ldev_blk_size-0b(%r13)
	sr      %r4,%r3
.Lmloop4:
# At this point: r4: number of blocks we want to write
#                r9: number of bytes we want to write
	lg      %r2,.Ldh_mem_size-0b(%r13)    # memory size
	sgr     %r2,%r10                      # bytes left to write
	clgr    %r2,%r9                       # is it last chunk?
	bh      .Lmloop5-0b(%r13)             # no, write whole chunk
	lr      %r5,%r2                       # yes, prepare to write last chunk
	lghi    %r4,0                         # re-init #blocks
	d       %r4,.Ldev_blk_size-0b(%r13)   # quotient is bit 32-63 of r5
	lr      %r4,%r5                       # #blocks left to write
.Lmloop5:
	lgr     %r2,%r11                      # restore r2
	lgr     %r3,%r10                      # restore r3
	bas     %r14,_ioblock_64-0b(%r13)     # write block to disk
	agr     %r10,%r9                      # update data address
	agr     %r11,%r4                      # skip to next block

	lgr     %r2,%r10                      # print progress to console
	bras    %r14,_print_progress_64

	clg     %r10,.Ldh_mem_size-0b(%r13)   # whole memory size written?
	bl      .Lmloop-0b(%r13)              # no, continue

# write end marker

.lendmarker:                                  # terminate dump file
	stck    .Ld_end_time-0b(%r13)         # store end time
	lgr     %r2,%r11                      # restore r2
	lghi    %r3,TMP_PAGE_START
	mvc     0(256,%r3),.Ld_endmarker-0b(%r13)
					      # copy dump end marker to page
					      # boundary
	lghi    %r4,1                         # write endmarker with one block
	bas     %r14,_ioblock_64-0b(%r13)     # write block to disk

	lmg     %r6,%r15,248(%r15)
	br      %r14                          # return to caller
.Lbytes_per_write:  .long 0x00000000
.Lheader_size:      .long HEADER_SIZE
.Lblocks_per_write: .word BLOCKS_PER_WRITE

################################################################################
# Get the device characteristics:
#  - r2: pointer to parameter table entry as written by zipl -M
#
# Pick up device characteristics from zipl parameter table (at PARAM_START).
# The following parameter format is expected (13 bytes):
#
# DDSS SSEE EEBN O
#
# - DD  : Devno of dump target
# - SSSS: Start block number of dump target
# - EEEE: End block number of dump target
# - B   : Blocksize of dump target (needs to be left-shifted by 8 bits)
# - N   : End Record Number
# - O   : Number of Heads of DASD
#
# We assume that the End Record Number is at track boundary.
# This allows us to determine the number of Blocks Per Track.
################################################################################

_get_device_characteristics_64:
	stmg   %r6,%r15,48(%r15)
	basr   %r13,0                       # base register
0:      aghi   %r15,-200                    # create stack frame

	mvc    .Ldev_devno-0b(10,%r13),0(%r2) # copy devno, start_blk, end_blk
	mvc    .Ldev_blk_size-0b+2(1,%r13),10(%r2)
	mvc    .Lblk_per_track-0b+3(1,%r13),11(%r2)
	mvc    .Ldev_nr_hds-0b+3(1,%r13),12(%r2)

	# Convert hexadecimal devno for "Dumping to <devno>" message
	unpk   .Lmsg_devno-0b(5,%r13),0(3,%r2) # unpack devno into message text
	la     %r1,.Ltrtab-0b(%r13)         # provide translation rule for...
	ahi    %r1,-0xf0                    # ...hexadecimal characters
	tr     .Lmsg_devno-0b(4,%r13),0(%r1)# translate devno in message text
	mvi    .Lmsg_devno_end-0b(%r13),0x00# insert null into message

	# Obtain subchannel number corresponding to devno
	iilh   %r1,1                        # initialize subsys-ident word
	iill   %r1,0                        # initialize subchannel number
	lr     %r6,%r1                      # duplicate register
	iill   %r6,0xffff                   # highest subchannel number
.Lschloop:
	stsch  .Ltmp_data-0b(%r13)          # try to obtain info for subchannel
	bnz    .Lschnop-0b(%r13)            # sch not operational, try next
	clc    .Ltmp_data-0b+6(2,%r13),0(%r2) # got right devno?
	be     .Lschdone-0b(%r13)           # yes, we're done
.Lschnop:
	la     %r1,1(%r1)                   # increment subchannel number
	cr     %r1,%r6                      # upper limit reached?
	bnh    .Lschloop-0b(%r13)           # no, continue loop
	# issue "<devno> is undefined" message and exit
	mvi    .Lmsg_devno_end-0b(%r13),0x40# insert blank into message
	la     %r2,.Lmsg_devno-0b(%r13)     # point to message
	bras   %r14,_sclp_print             # print message
	lghi   %r2,ENODEV                   # set error code
	b      _panik_64-0b(%r13)           # exit
.Lschdone:
	st     %r1,.Ldev_ssid-0b(%r13)      # store subsys-identification word
	lr     %r2,%r1                      # provide input register
	llgf   %r14,.Lenable_device_64-0b(%r13)
	basr   %r14,%r14
	lhi    %r2,CCW_CHAIN_START          # point to 1st CCW in chain
	l      %r5,.Ldev_blk_size-0b(%r13)  # get blocksize
	stcm   %r5,3,18(%r2)                # store blocksize into 1st write CCW
	stcm   %r5,3,.Llodata+14-0b(%r13)   # store blocksize into lodata
	lmg    %r6,%r15,248(%r15)
	br     %r14

################################################################################
# This function reads or writes a block number given in r2 to disk
#  -r2:  number of first block to write ( input by caller )
#        We start counting with Block Nr 0 !!!
#  -r3:  address to write data from ( input by caller )
#  -r4:  number of blocks to write ( input by caller )
################################################################################

_ioblock_64:
	stmg    %r4,%r15,48(%r15)
	basr    %r13,0                        # base register
0:      aghi    %r15,-200                     # create stack frame

	lghi    %r12,CCW_CHAIN_START
	st      %r12,8+.Lorb-0b(%r13)         # store cpaddr to orb
	la      %r12,16(%r12)                 # skip DE and LOC CCW
	oi      1(%r12),0x44                  # set chain-command+ida flag
	lghi    %r10,IDA_LIST_START           # pointer to ida list

	# setup new pgc psw for finding invalid pages
	mvc     .Lold_pgc_psw-0b(16,%r13),464(%r0)      # save old psw
	mvc     464(16,%r0),.Lpage_invalid_psw-0b(%r13) # setup pgm check new

        # first setup the write channel program
	lgr     %r11,%r4                      # save number of blocks
	llgf    %r6,.Lblk_per_track-0b(%r13)  # load blocks per track to r6
	llgf    %r7,.Ldev_nr_hds-0b(%r13)     # load heads to r7
	la      %r5,.Lwrloop-0b(%r13)
	br      %r5

2:      # loop for number of block (nbl) time

	# switch to next write CCW
	llgf    %r5,.Ldev_blk_size-0b(%r13)
	agr     %r3,%r5                       # add blksize to destination addr
	mvc     8(8,%r12),0(%r12)             # copy ccw to next ccw
	ahi     %r12,8                        # add sizeof(ccw) to base address
	ahi     %r10,8                        # next element in ida list

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
	llgf    %r2,.Ldev_ssid-0b(%r13)       # subchannel id
	la      %r3,.Lorb-0b(%r13)
	la      %r4,.Ltmp_data-0b(%r13)       # irb
	la      %r5,10                        # initialize retries

	bas     %r14,_ssch_64-0b(%r13)        # start I/O

	mvc     464(16,%r0),.Lold_pgc_psw-0b(%r13) # restore pgm check new
	lmg     %r4,%r15,248(%r15)
	br      %r14                          # return to caller
.align 8
.Lorb:
	.long 0x0049504c,0x0082ff00
	.long 0x00000000,0x00000000
.Ldedata:
	.long 0x00c00000,0x00000000           # define extent parameters
	.long 0x00000000,0x00000000
.Llodata:
	.long 0x00800001,0x00000000           # locate record parameters
	.long 0x00000000,0x03000000
	.long 0x12345678,0x00000000
.align 8
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

################################################################################
# DATA
################################################################################

.Lc96:  .long  96 # for creating stackframes

# device characteristics
.align 8
# Do *not* permutate the sequence of the following three definitions
.Ldev_devno:
	.short 0x0000
.Ldev_start_blk:
	.long  0x00000000
.Ldev_end_blk:
	.long  0x00000000
.Ldev_blk_size:
	.long  0x00000000
.Ldev_nr_hds:
	.long  0x00000000
.Lblk_per_track:
	.long  0x00000000
.Ldev_full_flag:
	.short 0x0000
.Ldev_ssid:
	.long  0x00000000

.Lparam_ptr:
	.long  0x00000000
.Lmem_current:
	.long  0x00000000,0x00000000

#
# CCWs
#
.align 8

# channel program for one write
.Ldeccw:
	.long 0x63400010,0x00000000+.Ldedata     # define extent
.Lloccw:
	.long 0x47400010,0x00000000+.Llodata     # locate record

# Translation table
.Ltrtab:
	.long 0xf0f1f2f3,0xf4f5f6f7,0xf8f98182,0x83848586

#
# Messages
#

# "Dumping to: 0000" and "0000 is undefined"
.Lmsg_dump2devno:
	.byte 0xc4, 0xa4, 0x94, 0x97, 0x89, 0x95, 0x87, 0x40
	.byte 0xa3, 0x96, 0x7a, 0x40
.Lmsg_devno:
	.byte 0x00, 0x00, 0x00, 0x00
.Lmsg_devno_end:
	.byte 0x00
	.byte 0x89, 0xa2, 0x40, 0xa4, 0x95, 0x84, 0x85, 0x86
	.byte 0x89, 0x95, 0x85, 0x84
	.byte 0x00
# "Wrong signature"
.Lmsg_signature:
	.byte 0xe6, 0x99, 0x96, 0x95, 0x87, 0x40, 0xa2, 0x89
	.byte 0x87, 0x95, 0x81, 0xa3, 0xa4, 0x99, 0x85
	.byte 0x00
# "Inconsistent time stamps"
.Lmsg_time_stamp:
	.byte 0xc9, 0x95, 0x83, 0x96, 0x95, 0xa2, 0x89, 0xa2
	.byte 0xa3, 0x85, 0x95, 0xa3, 0x40, 0xa3, 0x89, 0x94
	.byte 0x85, 0x40, 0xa2, 0xa3, 0x81, 0x94, 0x97, 0xa2
	.byte 0x00

.org (MVDUMP_TOOL_SIZE - 10)
.Lforce:
	.byte 0x00
	.byte 0x00                            # is set to 0x01 by zipl -f
.Lmem_upper_limit:
	.long 0xffffffff,0xffffffff           # can be used for memsize=xxx
# Provide storage for parameter table: 10 + 32*PTE_LENGTH = 426 bytes
# We take 512 bytes to match with struct mvdump_parm_table in include/boot.h
	.fill  512,1,0

#else /* __s390x__ */
	.globl _start
_start:
	basr  %r13,0                          # dummy data for 31-bit

#endif
