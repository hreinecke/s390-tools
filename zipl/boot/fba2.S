#
#  Second stage boot loader for fba devices.
#    Copyright IBM Corp. 2001, 2006.
#    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
#

#
# An fba blocklist is 16 bytes in length and contains 3 entries:
#   offset 0  : unused (32 bit)
#   offset 4  : BBBB block number (32 bit)
#   offset 8  : SS size of blocks (16 bit)
#   offset 10 : NN number of blocks (16 bit) - 1
#   offset 12 : unused (32bit)
#

STAGE2_DESC = 0x78

#include "common.S"

# expand the common start code from iplcommon.s
	stage2_start
	
# expand blocklist traversing code from iplcommon.s
        blocklist_traverser

# expand enable/disable routines
	device_fn
	panik

# parameter
#   %r2    : blocklist descriptor address
#	     16 bytes 00000000 BBBB SS NN 00000000
# returns
#   %r2    : number of bytes (blocksize * number of blocks)
_extract_length:
	l      %r3,8(%r2)
	lr     %r2,%r3
	srl    %r2,16
	sll    %r3,16
	srl    %r3,16
	la     %r3,1(%r3)
	msr    %r2,%r3
	br     %r14

# parameter
#   %r2    : blocklist descriptor address
#	     16 bytes 00000000 BBBB SS NN 00000000
# returns
#   %r2    : block number
_is_zero_block:
	l      %r2,4(%r2)
	br     %r14

# parameter
#   %r2    : address of blocklist descriptor
#	     16 bytes 00000000 BBBB SS NN 00000000
#   %r4    : device subchannel id
#   %r5    : load address
# returns
#   %r2    : updated load address
_load_direct:
        stm    %r6,%r15,24(%r15)
        basr   %r13,0                   # base register
.Lbase:	s      %r15,.Lc96-.Lbase(%r13)  # create stack frame
	lr     %r12,%r5                 # save load address
	lr     %r11,%r4                 # save subchannel id
	l      %r10,8(%r2)		# load number of blocks/blocksize
	n      %r10,.Lc65535-.Lbase(%r13)       # low word = number of blocks
	la     %r10,1(%r10)             # add 1 to number of blocks
	l      %r9,8(%r2)		# load number of blocks/blocksize
	srl    %r9,16                   # high word = size of blocks
	l      %r8,4(%r2)               # save block number
.Lmain: # main loop
	ltr    %r0,%r10                 # any blocks left ?
	bnp    .Lexit-.Lbase(%r13)
	cl     %r0,.Lc128-.Lbase(%r13)  # more than 128 blocks left ?
	bnh    .Lblks-.Lbase(%r13)
	l      %r0,.Lc128-.Lbase(%r13)  # limit to 128 blocks in one go
.Lblks: sth    %r9,.Lde+2-.Lbase(%r13)  # store block size to define extent
	st     %r8,.Lde+4-.Lbase(%r13)  # store start block to define extent
        alr    %r8,%r0                  # increase start block
	slr    %r10,%r0                 # reduce number of blocks left
	lr     %r1,%r0
	bctr   %r1,0                    # decrease number of blocks by 1
	st     %r1,.Lde+12-.Lbase(%r13) # store last block to define extent
	l      %r1,.Lprd-.Lbase(%r13)   # load address of ccw area
	l      %r2,.Lplo-.Lbase(%r13)   # load address of locate area
        mvc    0(8,%r1),.Lskde-.Lbase(%r13)     # copy DE ccw to chain
	la     %r1,8(%r1)
	slr    %r3,%r3
        b      .Llpst-.Lbase(%r13)
.Lloop:	oi     9(%r1),0x40              # set command chain bit
	la     %r1,16(%r1)              # update ccw pointer
	la     %r2,8(%r2)               # update locate pointer
.Llpst:	mvc    0(16,%r1),.Lskrd-.Lbase(%r13)    # copy locate/read ccw to chain
        mvc    0(8,%r2),.Lsklo-.Lbase(%r13)     # copy locate record skeleton
	st     %r2,4(%r1)               # store locate record address to ccw
	st     %r3,4(%r2)               # store logical block number to locate
        sth    %r9,10(%r1)              # store block size to read ccw
	st     %r12,12(%r1)             # store load address to read ccw
	alr    %r12,%r9                 # add block size to load address
        la     %r3,1(%r3)               # increase block counter
	bct    %r0,.Lloop-.Lbase(%r13)
	ni     9(%r1),0xbf              # clear command chain bit
	lr     %r2,%r11                 # pass subchannel id
	la     %r3,.Lorb-.Lbase(%r13)   # pass address of orb
	la     %r4,.Lirb-.Lbase(%r13)   # and pass address of irb
        bas    %r14,_ssch-.Lbase(%r13)  # read up to 128 blocks
	b      .Lmain-.Lbase(%r13)
.Lexit:	lr     %r2,%r12                 # return updated load address
        lm     %r6,%r15,120(%r15)
        br     %r14
.Lc1:	.long  1
.Lc96:	.long  96
.Lc128:	.long  128
.Lc65535:
	.long  65535
.Lprd:	.long  0x4000
.Lplo:	.long  0x6000
        .align 8
.Lskrd:	.long  0x43400008,0x00000000    # locate ccw skeleton
	.long  0x42200000,0x00000000    # read ccw skeleton
.Lskde: .long  0x63400010,.Lde          # define extent ccw skeleton
.Lsklo:	.long  0x06000001,0x00000000    # locate record skeleton
# offset 2 in locate: block count, offset 4 in locate: block number
.Lde:	.long  0x40000200,0x00000000    # offset 4 in DE: extent locator
        .long  0x00000000,0x00000000    # offset 8 in DE: first logical block
	                                # offset 12 in DE: last logical block
.Lirb:	.fill  64,1,0
.Lorb:	.long  0x00000000,0x0080ff00,0x4000,0

# expand io subroutines from iplcommon.s
	io_subroutines

	# boot menu parameter area follows
	menu_param_area
