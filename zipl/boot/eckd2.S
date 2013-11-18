
#
#  Second stage boot loader for ckd/eckd devices.
#    Copyright IBM Corp. 2001, 2006.
#    Author(s):	 Martin Schwidefsky (schwidefsky@de.ibm.com),
#                Antoinette Kaschner (anto@de.ibm.com),
#
# An ckd/eckd  loadlist  entry is 16 bytes in length and contains 4 entries:
#   offset 0 :	 CC HH R  (cyl head record 40bit)
#   offset 5 :	 LL record length (16 bit)
#   offset 7 :	 N number of records(8 bit)
#   offset 8 :	 unused(64 bit)

STAGE2_DESC = 0x78

#include "common.S"
	
# expand the common start code from iplcommon.s
        stage2_start
	
# expand blocklist traversing code from iplcommon.s
        blocklist_traverser
	
#
# expand enable/disable routines
        device_fn
	panik

# parameter
#   %r2    : blocklist descriptor address
#	     16 byte CCHHR LL N 00000000
# returns
#   %r2    : number of bytes (blocksize * number of blocks)
_extract_length:
	l      %r3,4(%r2)
	lr     %r2,%r3
	sll    %r2,8
	srl    %r2,16
	sll    %r3,24
	srl    %r3,24
	la     %r3,1(%r3)
	msr    %r2,%r3
	br     %r14

# parameter
#   %r2    : blocklist descriptor address
#	     16 byte CCHHR LL N 00000000
# returns
#   %r2    : == 0 for normal block descriptor
#            != 0 for zero block descriptor
_is_zero_block:
	l      %r3,4(%r2)
	l      %r2,0(%r2)
	srl    %r3,24
	or     %r2,%r3
	br     %r14

# parameter
#   %r2    :	 address of recordlist descriptor
#	         16 byte CCHHR LL N 00000000
#   %r4    :	 device subchannel id
#   %r5    :	 load address
_load_direct:	
        stm    %r6,%r15,24(%r15)        # save callers register
        basr   %r13,0                   # base register
.Lbase:	
	s      %r15,.Lc96-.Lbase(%r13)  # new save area address
        lr     %r12,%r5                 # save load address
        lr     %r11,%r4                 # save subchannel id
	l      %r3,4(%r2)		# get load descriptor from address
	l      %r2,0(%r2)		# get load descriptor from address
	stm    %r2,%r3,.Lsearch-.Lbase(%r13)         # get listdescriptor into search argument
        slr    %r10,%r10                # clear reg 10
	icm    %r10,1,.Lsearch+7-.Lbase(%r13)        # get record  number
	la     %r10,1(%r10)             # add 1 to prevent negative value
        slr    %r9,%r9                  # clear reg 9
	icm    %r9,3,.Lsearch+5-.Lbase(%r13)       # get record size
	sth    %r9,.Lread+2-.Lbase(%r13)  # store record size in read ccb
        l      %r1,.Lprd-.Lbase(%r13)       # load address of ccw area
        mvc    0(24,%r1),.Lccws-.Lbase(%r13)     # copy seek/search/tic ccws to chain
	la     %r8,8(%r1)               # get address of search command
	st     %r8,20(%r1)               # store  search address in tic	
        la     %r1,24(%r1)              # advance ccw chain pointer
	slr    %r3,%r3
        b      .Llpst-.Lbase(%r13)
.Lloop:
	oi     1(%r1),0x40              # set command chain bit
        la     %r1,8(%r1)               # update ccw pointer
.Llpst:	
	mvc    0(8,%r1),.Lread-.Lbase(%r13)    # copy read ccw to chain
        st     %r12,4(,%r1)             # store load address to read ccw
        alr    %r12,%r9                 # add record size to load address
        bct    %r10,.Lloop-.Lbase(%r13)
        ni     1(%r1),0xbf              # clear command chain bit
        la     %r3,.Lorb-.Lbase(%r13)   # pass address of orb
        la     %r4,.Lirb-.Lbase(%r13)   # and pass address of irb
	lr     %r2,%r11                 # pass subchannel id
        bas    %r14,_ssch-.Lbase(%r13)  # read records
.Lexit:
        lr     %r2,%r12                 # return updated load address
        lm     %r6,%r15,120(%r15)
        br     %r14
.Lc1:	   .long  1
.Lc96:	  .long  96
.Lc65535:	
        .long  65535
.Lprd:  .long  0x4000
        .align 8
.Lccws:	
        .long 0x07600006,0x00000000+.Lseekarg	     # seek for CYL and TRK
.Lsrch:	
        .long 0x31600005,0x00000000+.Lsearch        # search for record
	
        .long 0x08000000,0x00000000                 # tic
.Lread:	
        .long 0x86200000,0x00000000                 # read record into storage
	
.Lseekarg:
	.word 0x0000                    # seek argument  
                                        # format 00CCHH
.Lsearch:	
	.long 0x00000000,0x00000000     # search argument
	#  format CCHHRLLn       	

        .align 8   
.Lirb:  .fill  64,1,0
.Lorb:  .long  0x00000000,0x0080ff00,0x4000,0

# expand io subroutines from iplcommon.s
        io_subroutines

	# boot menu parameter area follows
	menu_param_area

