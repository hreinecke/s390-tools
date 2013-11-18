#
#  Common code for the different second stage boot loaders
#    Copyright IBM Corp. 2001, 2006.
#    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
#		Heiko Carstens (heiko.carstens@de.ibm.com)
#

# some definitions from kernel land
__LC_IRB = 0x0300
__LC_IPLDEV  = 0xC6C
__LC_PANIC_MAGIC= 0xE00

# Error codes shown as address in disabled wait psw

#include "error.h"
#include "kdump.S"

	.macro stage2_start
#
# Function entry point at 0x2000 is called with C linkage
#   %r2-%r3: load descriptor
#   %r3    : device subchannel id
#   %r4    : load address
#
        basr   %r1,0
        b      _load_blocklist-.(%r1)
	.align 8

#
# Program execution of the second stage boot loader starts at 0x2008
#
        .globl _start
_start: stm    %r0,%r15,0x180           # store all registers 
        basr   %r13,0
0:      l      %r15,3f-0b(%r13)         # load end of stack address
	mvc    .Lstage2-0b(16,%r13),STAGE2_DESC # save stage2_desc from lowcore

	bas    %r14,_menu-0b(%r13)	# show menu
	lr     %r6,%r2
	bas    %r14,_kdump_stage2-0b(%r13)
	sll    %r6,4			# get offset from config number

        l      %r11,0xb8                # load ipl device subchannel id
	lr     %r2,%r11
	bas    %r14,_enable_device-0b(%r13)
	ltr     %r2,%r2			# lpum valid?
	bz     6f-0b(%r13)
	stc    %r2,.Llpm-0b(%r13)	# save lpum
6:
        la     %r10,0
        st     %r10,__LC_PANIC_MAGIC    # clear panic flag
	la     %r2,.Lstage2-0b(%r13)    # load blockpointer address
	lr     %r4,%r11			# load sid
	l      %r5,4f-0b(%r13)	        # load load-address
	la     %r12,0(%r5)              # FIXME: provide the correct offset
        bas    %r14,_load_direct-0b(%r13)
	ar     %r6,%r12
        la     %r2,16(%r6)              # get load descriptors address
        lr     %r4,%r11
        lr     %r5,%r12                 # set load address
        bas    %r14,_load_direct-0b(%r13)
	la     %r12,32(%r12)		# skip header entry
1:      la     %r2,0(%r12)		# load address of descriptor
	l      %r4,20(%r12)		# load type
	lhi    %r5,0xf
	nr     %r4,%r5			# only the last four bits are the type
	l      %r5,28(%r12)		# load address
	l      %r6,24(%r12)		# load psw mask
	sll    %r4,2
	l      %r1,5f-0b(%r4,%r13)	# function pointer according to type
	lr     %r4,%r11			# pass subchannel id
        bas    %r14,0(%r1)
2:	la     %r12,32(%r12)		# next load descriptor
        b      1b-0b(%r13)
3:      .long  0x10000-96
4:	.long  0x8000
5:	.long  _bug			# type 0: not allowed
	.long  _execute			# type 1: jump to target
	.long  _load_binary		# type 2: binary file
.Lstage2:				# area to save stage2_desc
	.long  0x00000000,0x00000000
	.long  0x00000000,0x00000000
	.align 2

_bug:
	basr   %r1,0
0:	l      %r2,1f-0b(%r1)
	bas    %r4,_panik-0b(%r1)	# won't return
        br     %r14
1:	.long  EWRONGTYPE


	kdump_base
	kdump_stage2

_load_binary:
        stm    %r6,%r15,24(%r15)
        basr   %r13,0                   # base register
0:      s      %r15,1f-0b(%r13)         # create stack frame
        lr     %r12,%r4
	bas    %r14,_load_blocklist-0b(%r13)
        lm     %r6,%r15,120(%r15)
        br     %r14
1:	.long  96
	
_execute:
	basr   %r1,0
0:	st     %r6,0(%r0)		# write supplied psw to address 0
	st     %r5,4(%r0)
	lr     %r2,%r11
	bas    %r14,_disable_device-0b(%r1)
	lpsw   0			# execute what we have loaded

#include "menu.S"

	.endm

#
# The load descriptor is 32 bytes in length and contains 3 entries:
#   offset 0  : a blocklist descriptor (fba/eckd, 128 bit)
#   offset 23 : a type (8 bit)
#   offset 24 : an address (64 bit)
# The meaning of the address and the validity of the blocklst
# depends on the type.
#   type = 1 : load binary, blocklist valid, address = load address
#   type = 2 : load psw, blocklist invalid, address = PSW
#
	.macro blocklist_traverser
# parameter
#   %r2    : blocklist head descriptor address
#   %r4    : device subchannel id
#   %r5    : load address
_load_blocklist:
        stm    %r6,%r15,24(%r15)
        basr   %r13,0                   # base register
0:      s      %r15,7f-0b(%r13)         # create stack frame
1:	lr     %r12,%r4                 # save device subchannel id
	lr     %r11,%r5                 # save load address
	lr     %r8,%r2                  # save descriptor address
	bas    %r14,_extract_length-0b(%r13)   # get length from descriptor
	lr     %r10,%r2                 # save returned length
	lr     %r2,%r8                  # reload descriptor address to %r2
	lr     %r4,%r12                 # reload device id to %r4
	l      %r5,9f-0b(%r13)          # get memory area for indirect block
        bas    %r14,_load_direct-0b(%r13) # load indirect block
	lr     %r9,%r10                 # (length / 8 - 1) = # direct descr.
	srl    %r9,4
	bctr   %r9,0
	l      %r8,9f-0b(%r13)
2:	clc    0(8,%r8),8f-0b(%r13)     # test block descriptor
	be     6f-0b(%r13)              # descriptor == 0 -> done
	la     %r2,0(%r8)               # pass descriptor address
	bas    %r14,_is_zero_block-0b(%r13)    # test for special 0 descriptor
	ltr    %r2,%r2                  # is it a hole to fill with 0?
	bne    4f-0b(%r13)              # no, normal block descriptor
	la     %r2,0(%r8)               # pass descriptor address
	bas    %r14,_extract_length-0b(%r13)   # get length from descriptor
	lr     %r3,%r2                  # move length to an odd register
	lr     %r2,%r11                 # move address to an even register
	alr    %r11,%r3                 # add length to load address
	slr    %r5,%r5                  # set length of source to zero
3:	mvcle  %r2,%r4,0                # clear storage
	bo     3b-0b(%r13)	
	b      5f-0b(%r13)
4:	la     %r2,0(%r8)               # pass descriptor address
	lr     %r4,%r12                 # pass device subchannel id
        lr     %r5,%r11                 # pass load address
        bas    %r14,_load_direct-0b(%r13) # load indirect block
	lr     %r11,%r2                 # move updated load address
5:	la     %r8,16(%r8)               # next descriptor
	bct    %r9,2b-0b(%r13)
	la     %r2,0(%r8)               # load continuation descriptor address
	lr     %r4,%r12                 # move device id for next round
	lr     %r5,%r11			# move load address for next round
	clc    0(8,%r8),8f-0b(%r13)     # test continuation descriptor
	bne    1b-0b(%r13)              # != 0 -> load next indirect block
6:      lr     %r2,%r11                 # return updated load address
	lm     %r6,%r15,120(%r15)
        br     %r14
7:      .long  96
8:      .long  0,0
9:      .long  0x8200                   # memory address for indirect blocks

        .endm

	.macro device_fn
#
# Enable I/O on the ipl device. 
#   %r2 : device subchannel id
# Return:
#   %r2 : lpum of subchannel
#
_enable_device:
        stm    %r6,%r15,24(%r15)
        basr   %r13,0                   # base register
0:      s      %r15,4f-0b(%r13)
	lr     %r1,%r2
	l      %r2,7f-0b(%r13)          # set panik code early
        stsch  5f-0b(%r13)
	brc    1,3f		        # panic if not operational
        oi     5f-0b+5(%r13),0x80       # enable subchannel
	lhi    %r6,256			# r6 retry counter
1: # modify subchannel
        msch   5f-0b(%r13)
	brc    6,2f        		# status pending or busy
	brc    1,3f		        # panic if not operational
	lctl   %c6,%c6,6f-0b(%r13)      # enable all interrupt classes
	sr     %r2,%r2
	ic     %r2,10+5f-0b(%r13)	# return lpum in r2
        lm     %r6,%r15,120(%r15)
        br     %r14
2: # clear status and retry
	tsch   __LC_IRB
	brc    1,3f		        # panic if not operational
	brct   %r6,1b			# retry
3: # panic
	b      _panik-0b(%r13)          # panic
4:	.long  96
	.align 8
5:	.fill  64,1,0
6:	.long  0xff000000               # CR6 initial value
7:      .long  EENABLE_DEV

#
# Disable I/O on the ipl device.
#   %r2 : device subchannel id
#
_disable_device:
        stm    %r6,%r15,24(%r15)
        basr   %r13,0                   # base register
0:      s      %r15,4f-0b(%r13)
	lr     %r1,%r2
	l      %r2,6f-0b(%r13)          # set panik code early
	lctl   %c6,%c6,5f-0b(%r13)      # disable all interrupt classes
        stsch  5f-0b(%r13)
	brc    1,3f			# panic if not operational
        ni     5f-0b+5(%r13),0x7F       # disable subchannel
	lhi    %r6,256			# r6 retry counter
1: # modify subchannel
        msch   5f-0b(%r13)
	brc    6,2f			# status pending or busy
	brc    1,3f			# panic if not operational
        lm     %r6,%r15,120(%r15)
        br     %r14
2: # clear status and retry
	tsch   __LC_IRB
	brc    1,3f			# panic if not operational
	brct   %r6,1b
3: # panic
	b      _panik-0b(%r13)          # panic
4:	.long  96
        .align 8
5:	.long  0x00000000               # CR6 (all interrupts classes disabled)
6:      .long  EDISABLE_DEV
	.endm	

	.macro io_subroutines


__LC_IO_NEW_PSW		= 0x78
__LC_SUBCHANNEL_ID	= 0xb8

#
# Wait for I/O interrupt.
#
# Wait until status for the specified subchannel is available.
#
#   %r2 : subchannel id
#   %r3 : address of irb
#

_wait_for_int:
	lr     %r1,%r2
	basr   %r4,0			# get base register
0:
	mvc    __LC_IO_NEW_PSW(8),4f-0b(%r4) # set i/o new psw
1: # wait
	lpsw   3f-0b(%r4)
2: # continue
	tsch   0(%r3)			# get status
	brc    4,1b			# if cc=1 goto wait
	br     %r14			# return
        .align 8
3:
	.long  0x020a0000,0x80000000+1b	# enabled wait psw
4:
	.long  0x00080000,0x80000000+2b	# io new psw

#
# Start I/O
#
# Attempt to start I/O defined by ORB on specified subchannel. Retry I/O
# 256 times per path (recommended error recovery procedure for IFCCs) unless
# a permanent path error condition is indicated. Try all paths twice to try to
# work around "link flapping" (paths going down once each in the same order as
# they are tried). Perform CLEAR SUBCHANNEL when switching paths to clear any
# improper subchannel status.
#
#   %r2 : subchannel id
#   %r3 : address of orb
#   %r4 : address of irb
#
_ssch:
	stm    %r6,%r15,24(%r15)
	basr   %r13,0			# get base register
0:
	ahi    %r15,-96			# create stack frame
	lr     %r6,%r2			# r6:  sid
	lr     %r7,%r3			# r7:  orb
	lr     %r8,%r4			# r8:  irb
	sr     %r9,%r9			# r9:  initial lpm
	ic     %r9,.Llpm-0b(%r13)
	l      %r10,.Lmask-0b(%r13)	# r10: path mask
1: # restart_all
	lhi    %r11,256			# r11: retry counter
2: # restart
	stc    %r9,6(%r7)		# store initial lpm in orb
	ltr    %r9,%r9			# if non-zero initial lpm
	jne    3f			# then use initial lpm
	stc    %r10,6(%r7)		# else use current path mask
3: # gotlpm
	lr     %r1,%r6			# get sid
	ssch   0(%r7)			# start subchannel
	brc    1,7f			# if cc==3 goto next_path
	brc    7,6f                     # if cc!=0 goto retry
4: # wait_for_int_loop
	lr     %r2,%r6			# get sid
	lr     %r3,%r8			# get irb
	bras   %r14,_wait_for_int	# wait for interrupt
	jnz    9f			# if cc!=0 goto panic
	tm     0(%r8),0x3		# test irb deferred cc
	brc    1,7f			# if cc==3 goto next_path
	jz     5f			# if cc==0 goto no_stctl_check
	tm     3(%r8),0x10		# test irb status control
	jnz    6f			# if alert goto retry
5: # no_stctl_check
	tm     9(%r8),0xff		# test irb subchannel status
	jnz    6f			# if status!=0 goto retry
	tm     8(%r8),0xf3		# test irb unusual device status
	jnz    6f			# if status!=0 goto retry
	tm     8(%r8),0x4		# test irb device end
	jz     4b			# if !device_end goto wait_for_int_loop
	lm     %r6,%r15,120(%r15)
	br     %r14			# return

6: # retry
	lr     %r1,%r6			# get sid
	tsch   0(%r8)			# clear status if necessary
	brct   %r11,2b			# if --retries>0 goto restart
7: # next_path
	ltr    %r9,%r9			# if initial lpm != 0
	jnz    8f			# then goto noshift
	srl    %r10,1			# path_mask >>= 1
	ltr    %r10,%r10		# if path_mask==0
	jz     9f			# then goto panic
8: # noshift
	sr     %r9,%r9			# clear initial lpm
	lr     %r1,%r6			# get sid
	csch				# clear subchannel
	brc    7,9f			# if cc!=0 goto panic
	lr     %r2,%r6			# get sid
	lr     %r3,%r8			# get irb
	bras   %r14,_wait_for_int	# wait for interrupt
	j      1b			# goto restart_all
9: # panic
	l      %r2,.Lerrno-0b(%r13)	# get error code
	bras   %r14,_panik		# panic

.Lerrno:
	.long  ESSCH
.Lmask:
	.long  0x8080
.Llpm:
	.byte  0x00
	.align 2
	.endm

	.macro panik
	.align 2
#
# Panik routine. Loads a disabled wait psw
#   %r2 : panik code
#
_panik:
#if defined(__s390x__)
	larl   %r13,1f
	st     %r2,12(%r13)             # store code in address part of psw
	brasl  %r14,_switch_64
	larl   %r13,1f
	lpswe  0(%r13)
	.align 8
1:	.long  0x00020000,0x80000000,0x00000000,0x00000000
#else
	basr   %r1,0
0:	st     %r2,1f-0b+4(%r1)         # store code in address part of psw
	lpsw   1f-0b(%r1)
	.align 8
1:	.long  0x000a0000,0x00000000
#endif
	.endm
