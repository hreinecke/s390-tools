#
# s390-tools/zipl/boot/sclp.S
#   Mini SCLP driver.
#
# Copyright IBM Corp. 2004, 2012
#
# Author(s): Peter Oberparleiter <peter.oberparleiter@de.ibm.com>
#            Michael Holzheu <holzheu@vnet.linux.ibm.com>
#

#ifdef USE_64BIT_SCLP
LC_EXT_NEW_PSW		= 0x1b0			# addr of ext int handler
PSW_SIZE                = 16
#else
LC_EXT_NEW_PSW		= 0x58			# addr of ext int handler
PSW_SIZE                = 8
#endif
LC_EXT_INT_PARAM	= 0x80			# addr of ext int parameter
LC_EXT_INT_CODE		= 0x86			# addr of ext int code

SCCB_ADDR		= 0x8000		# addr of sccb work area

SCCB_LENGTH		= 0			# offset of length field
SCCB_RESPONSE		= 6			# offset of response field
SCCB_DATA		= 8			# offset of data
SCCB_PARAM		= 24			# offset of load parameter

EVBUF_LENGTH		= 0			# offset of length field
EVBUF_TYPE		= 2			# offset of type field
EVBUF_DATA		= 6			# offset of data

GDS_ID_MDSMU		= 0x1310		# vector keys and ids
GDS_ID_CPMSU		= 0x1212
GDS_ID_TextCmd		= 0x1320
GDS_KEY_SelfDefTextMsg	= 0x31

SDIAS_EVSTATE             = 0x1f
SDIAS_EVSTATE_ALL_STORED  = 0x00
SDIAS_EVSTATE_PART_STORED = 0x10

.macro sclp_base

#
# Subroutine which waits synchronously until either an external interruption
# or a timeout occurs.
#
# Parameters:
#   R2  = 0 for no timeout, non-zero for timeout in (approximated) seconds
#
# Returns:
#   R2  = 0 on interrupt, 2 on timeout
#   R3  = external interruption parameter if R2=0
#

_sclp_wait_int:
	stm    %r6,%r15,24(%r15)		# save registers

	basr   %r13,0				# get base register
.LbaseS1:
	ahi    %r15,-96				# create stack frame

	la     %r8,LC_EXT_NEW_PSW		# register int handler
	mvc    .LoldpswS1-.LbaseS1(PSW_SIZE,%r13),0(%r8)
	mvc    0(PSW_SIZE,%r8),.LextpswS1-.LbaseS1(%r13)

	lhi    %r6,0x0200			# cr mask for ext int (cr0.54)
	ltr    %r2,%r2
	jz     .LsetctS1

	ahi    %r6,0x0800			# cr mask for clock int (cr0.52)

	stck   .LtimeS1-.LbaseS1(%r13)		# initiate timeout
	al     %r2,.LtimeS1-.LbaseS1(%r13)
	st     %r2,.LtimeS1-.LbaseS1(%r13)
	sckc   .LtimeS1-.LbaseS1(%r13)

.LsetctS1:
	stctl  %c0,%c0,.LctlS1-.LbaseS1(%r13)	# enable required interrupts
	l      %r0,.LctlS1-.LbaseS1(%r13)
	lhi    %r1,~(0x200 | 0x800)		# clear old values
	nr     %r1,%r0
	or     %r1,%r6				# set new value
	st     %r1,.LctlS1-.LbaseS1(%r13)
	lctl   %c0,%c0,.LctlS1-.LbaseS1(%r13)
	st     %r0,.LctlS1-.LbaseS1(%r13)
	lhi    %r2,2				# return code for timeout

.LloopS1:
#ifdef USE_64BIT_SCLP
	lpswe  .LwaitpswS1-.LbaseS1(%r13)	# wait until interrupt
#else
	lpsw   .LwaitpswS1-.LbaseS1(%r13)	# wait until interrupt
#endif

.LwaitS1:
	lh     %r7,LC_EXT_INT_CODE
	chi    %r7,0x1004			# timeout?
	je     .LtimeoutS1
	chi    %r7,0x2401			# service int?
	jne    .LloopS1

	sr     %r2,%r2
	l      %r3,LC_EXT_INT_PARAM

.LtimeoutS1:
	lctl   %c0,%c0,.LctlS1-.LbaseS1(%r13)	# restore interrupt setting

	# restore old handler
	mvc    0(PSW_SIZE,%r8),.LoldpswS1-.LbaseS1(%r13)

	lm     %r6,%r15,120(%r15)		# restore registers

	br     %r14				# return to caller

	.align 8

#ifdef USE_64BIT_SCLP
.LoldpswS1:
	.long  0, 0, 0, 0			# old ext int PSW
.LextpswS1:
	.long  0x00000001, 0x80000000, 0x00000000, 0x00000000+.LwaitS1	# PSW to handle ext int
.LwaitpswS1:
	.long  0x01020001, 0x80000000, 0x00000000, 0x00000000+.LloopS1	# PSW to wait for ext int

#else
.LoldpswS1:
	.long  0, 0				# old ext int PSW
.LextpswS1:
	.long  0x00080000, 0x80000000+.LwaitS1	# PSW to handle ext int
.LwaitpswS1:
	.long  0x010a0000, 0x00000000+.LloopS1	# PSW to wait for ext int

#endif
.LtimeS1:
	.quad  0				# current time
.LctlS1:
	.long  0				# CT0 contents


#
# Subroutine to synchronously issue a service call.
#
# Parameters:
#   R2  = command word
#   R3  = sccb address
#
# Returns:
#   R2  = 0 on success, 1 on failure
#   R3  = sccb response code if R2 = 0
#   R4  = external INT parm
#

_sclp_servc:
	stm    %r6,%r15,24(%r15)		# save registers
	ahi    %r15,-96				# create stack frame

	lr     %r6,%r2				# save command word
	lr     %r7,%r3				# save sccb address

.LretryS2:
	lhi    %r2,1				# error return code

	.insn  rre,0xb2200000,%r6,%r7		# servc
	brc    1,.LendS2			# exit if not operational
	brc    8,.LnotbusyS2			# go on if not busy

	sr     %r2,%r2				# wait until no longer busy
	bras   %r14,_sclp_wait_int

	j      .LretryS2			# retry

.LnotbusyS2:
	sr     %r2,%r2				# wait until result
	bras   %r14,_sclp_wait_int
	lr     %r4,%r3

	sr     %r2,%r2
	lh     %r3,SCCB_RESPONSE(%r7)

.LendS2:
	lm     %r6,%r15,120(%r15)		# restore registers

	br     %r14


#
# Subroutine to set up send and receive SCLP masks
#
# Parameters:
#   R2  = receive mask
#   R3  = send mask
#
_sclp_set_mask:
	entry_linkage
	basr   %r13,0				# get base register
0:	st     %r2,.LinitmaskS3-0b(%r13)	# set receive mask
	st     %r3,.LinitmaskS3+4-0b(%r13)	# set send mask
	exit_linkage
	br     %r14

#
# Subroutine to set up the SCLP interface.
#
# Parameters:
#   R2  = 0 to activate, non-zero to deactivate
#
# Returns:
#   R2  = 0 on success, non-zero on failure
#

_sclp_setup:
	entry_linkage

	basr   %r13,0				# get base register
.LbaseS3:

	l      %r6,.LsccbS0-.LbaseS3(%r13)	# prepare init mask sccb
	mvc    0(.LinitendS3-.LinitsccbS3,%r6),.LinitsccbS3-.LbaseS3(%r13)

	ltr    %r2,%r2				# initialization?
	jz     .LdoinitS3			# go ahead

	# clear masks
	xc     .LinitmaskS3-.LinitsccbS3(8,%r6),.LinitmaskS3-.LinitsccbS3(%r6)

.LdoinitS3:
	l      %r2,.LwritemaskS3-.LbaseS3(%r13)	# get command word
	lr     %r3,%r6				# get sccb address
	bras   %r14,_sclp_servc			# issue service call

	ltr    %r2,%r2				# servc successful?
	jnz    .LerrorS3

	chi    %r3,0x20				# write mask successful?
	jne    .LerrorS3

	# check masks
	la     %r2,.LinitmaskS3-.LinitsccbS3(%r6)

	l      %r1,0(%r2)			# receive mask ok?
	n      %r1,12(%r2)
	cl     %r1,0(%r2)
	jne    .LerrorS3

	l      %r1,4(%r2)			# send mask ok?
	n      %r1,8(%r2)
	cl     %r1,4(%r2)
	sr     %r2,%r2
	je     .LendS3

.LerrorS3:
	lhi    %r2,1				# error return code

.LendS3:
	exit_linkage

	br     %r14

.LwritemaskS3:
	.long  0x00780005			# SCLP command for write mask
.LinitsccbS3:
	.word  .LinitendS3-.LinitsccbS3
	.byte  0,0,0,0
	.word  0
	.word  0
	.word  4
.LinitmaskS3:
	.long  0x80000000
	.long  0x40000000
	.long  0
	.long  0
.LinitendS3:

.endm # sclp_base

.macro sclp_print

#
# Subroutine which prints a given text to the SCLP console.
#
# Parameters:
#   R2  = address of nil-terminated EBCDIC text
#
# Returns:
#   R2  = 0 on success, 1 on failure
#

_sclp_print:
	entry_linkage

	basr   %r13,0				# get base register
.LbaseS4:

	l      %r8,.LsccbS0-.LbaseS4(%r13)	# prepare write data sccb
	mvc    0(.LmtoS4-.LwritesccbS4,%r8),.LwritesccbS4-.LbaseS4(%r13)

	la     %r7,.LmtoS4-.LwritesccbS4(%r8)	# current mto addr
	sr     %r0,%r0
.LinitmtoS4:
	# initialize mto
	mvc    0(.LmtoendS4-.LmtoS4,%r7),.LmtoS4-.LbaseS4(%r13)
	lhi    %r6,.LmtoendS4-.LmtoS4		# current mto length

.LloopS4:
	ic     %r0,0(%r2)			# get character
	ahi    %r2,1

	ltr    %r0,%r0				# end of string?
	jz     .LfinalizemtoS4

	chi    %r0,0x15				# end of line (NL)?
	jz     .LfinalizemtoS4

	stc    %r0,0(%r6,%r7)			# copy to mto
	ahi    %r6,1
	j      .LloopS4

.LfinalizemtoS4:
	sth    %r6,0(%r7)			# update mto length

	lh     %r9,.LmdbS4-.LwritesccbS4(%r8)	# update mdb length
	ar     %r9,%r6
	sth    %r9,.LmdbS4-.LwritesccbS4(%r8)

	lh     %r9,.LevbufS4-.LwritesccbS4(%r8)	# update evbuf length
	ar     %r9,%r6
	sth    %r9,.LevbufS4-.LwritesccbS4(%r8)

	lh     %r9,0(%r8)			# update sccb length
	ar     %r9,%r6
	sth    %r9,0(%r8)

	ar     %r7,%r6				# update current mto adress
	ltr    %r0,%r0				# more characters?
	jnz    .LinitmtoS4

	l      %r2,.LwritedataS4-.LbaseS4(%r13)	# write data
	lr     %r3,%r8
	bras   %r14,_sclp_servc

	ltr    %r2,%r2				# servc successful?
	jnz    .LendS4

	chi    %r3,0x20				# write data successful?
	je     .LendS4

	lhi    %r2,1				# error return code

.LendS4:
	exit_linkage

	br     %r14

.LwritedataS4:
	.long  0x00760005			# SCLP command for write data
.LwritesccbS4:
	# sccb
	.word  .LmtoS4-.LwritesccbS4
	.byte  0
	.byte  0,0,0
	.word  0

	# evbuf
.LevbufS4:
	.word  .LmtoS4-.LevbufS4
	.byte  0x02
	.byte  0
	.word  0

.LmdbS4:
	# mdb
	.word  .LmtoS4-.LmdbS4
	.word  1
	.long  0xd4c4c240
	.long  1

	# go
.LgoS4:
	.word  .LmtoS4-.LgoS4
	.word  1
	.long  0
	.byte  0,0,0,0,0,0,0,0
	.byte  0,0,0
	.byte  0
	.byte  0,0,0,0,0,0,0
	.byte  0
	.word  0
	.byte  0,0,0,0,0,0,0,0,0,0
	.byte  0,0,0,0,0,0,0,0
	.byte  0,0,0,0,0,0,0,0

.LmtoS4:
	.word  .LmtoendS4-.LmtoS4
	.word  4
	.word  0x1000
	.byte  0
	.byte  0,0,0
.LmtoendS4:

.endm # sclp_print

.macro sclp_read

#
# Subroutine which reads text from the SCLP console.
#
# Parameters:
#   R2  = 0 for no timeout, non-zero for timeout in (approximated) seconds
#   R3  = destination address for input
#
# Returns:
#   R2  = 0 on input, 1 on failure, 2 on timeout
#   R3  = length of input data
#

_sclp_read:
	stm    %r6,%r15,24(%r15)		# save registers

	basr   %r13,0				# get base register
.LbaseS5:
	ahi    %r15,-96				# create stack frame

	l      %r9,.LsccbS0-.LbaseS5(%r13)	# get sccb address

	lr     %r10,%r2				# save timeout
	lr     %r11,%r3				# save destination address

.LretryS5:
	# prepare sccb
	mvc    0(.LreadevbufS5-.LreadsccbS5,%r9),.LreadsccbS5-.LbaseS5(%r13)

	l      %r2,.LreaddataS5-.LbaseS5(%r13)	# read data
	lr     %r3,%r9
	bras   %r14,_sclp_servc

	ltr    %r2,%r2				# servc successful?
	jnz    .LendS5

	chi    %r3,0x20				# read event data successful?
	je     .LprocessS5			# process input

	chi    %r3,0x60f0			# no buffer?
	lhi    %r2,1
	jne    .LendS5				# anything else is an error

	lr     %r2,%r10				# wait for input or timeout
	bras   %r14,_sclp_wait_int

	ltr    %r2,%r2				# got int?
	jz     .LretryS5
	j      .LendS5				# just timeout

.LprocessS5:
	la     %r1,SCCB_DATA(%r9)		# get event data end address
	ah     %r1,EVBUF_LENGTH(%r1)
	st     %r1,.LendarrayS5-.LbaseS5(%r13)

	la     %r9,EVBUF_DATA+SCCB_DATA(%r9)	# get event data address

	lr     %r3,%r11				# current dest address
	sr     %r8,%r8				# current level

.LloopS5:
	chi    %r8,-4				# at end of buffer?
	je     .LsuccessS5

	# at end of vector?
	c      %r9,.LendarrayS5-.LbaseS5(%r8,%r13)
	jl     .LcontinueS5

	ahi    %r8,-4				# go up one level
	j      .LloopS5
.LcontinueS5:

	chi    %r8,8				# check for subvector
	jh     .LsubvectorS5

	lh     %r6,0(%r9)			# get vector length
	lh     %r0,2(%r9)			# get vector ID
	lhi    %r7,4				# vector header is 4 bytes

	j      .LprocessvecS5

.LsubvectorS5:
	sr     %r6,%r6				# get subvector length
	ic     %r6,0(%r9)

	sr     %r0,%r0				# get subvector key
	ic     %r0,1(%r9)

	lhi    %r7,2				# subvector header is 2 bytes

.LprocessvecS5:
	cr     %r6,%r7				# check for empty vector?
	je     .LendprocessS5

	# check for matching vector
	c      %r0,.LkeyarrayS5-.LbaseS5(%r8,%r13)
	jne    .LendprocessS5

	chi    %r8,16				# check for text vector
	je     .LcopytextS5

	ahi    %r8,4				# go down one level

	lr     %r0,%r9				# get end address for vector
	ar     %r0,%r6
	st     %r0,.LendarrayS5-.LbaseS5(%r8,%r13)

	ar     %r9,%r7				# skip header
	j      .LloopS5

.LcopytextS5:
	sr     %r6,%r7				# get length without header
	ar     %r9,%r7				# get addr after header
	sr     %r0,%r0

.LcopyloopS5:
	ic     %r0,0(%r9)			# copy text to dest
	stc    %r0,0(%r3)

	ahi    %r9,1
	ahi    %r3,1
	brct   %r6,.LcopyloopS5

.LendprocessS5:
	ar     %r9,%r6				# continue with next vector
	j      .LloopS5

.LsuccessS5:
	sr     %r2,%r2
	sr     %r3,%r11

.LendS5:
	lm     %r6,%r15,120(%r15)		# restore registers

	br     %r14

.LreaddataS5:
	.long  0x00770005			# SCLP command for read data
.LendarrayS5:
	.long  0,0,0,0,0			# end addr array for read
.LkeyarrayS5:
	.long  GDS_ID_MDSMU			# vector ID/key array for read
	.long  GDS_ID_CPMSU
	.long  GDS_ID_TextCmd
	.long  GDS_KEY_SelfDefTextMsg
	.long  0x30
.LreadsccbS5:
	.word  0x1000
	.byte  0
	.byte  0,0,0
	.word  0
.LreadevbufS5:

.endm # sclp_read

.macro sclp_read_info

#
# Subroutine to perform sclp read info.
#
# Parameters:
#   none
#
# Returns:
#   R2  = address of sccb on success, 0 on failure
#

_sclp_read_info:
	entry_linkage

	basr   %r13,0				# get base register
.LbaseS6:

	lr     %r8,%r2				# save dest address
	l      %r6,.LsccbS0-.LbaseS6(%r13)	# get sccb address

	# prepare sccb
	mvc    0(.LinfosccbendS6-.LinfosccbS6,%r6),.LinfosccbS6-.LbaseS6(%r13)

	la     %r7,.LreadinfoS6-.LbaseS6(%r13)
.LretryS6:
	l      %r2,0(%r7)			# get command word
	ltr    %r2,%r2
	jz     .LerrorS6

	lr     %r3,%r6				# get sccb address
	bras   %r14,_sclp_servc

	ltr    %r2,%r2				# servc successful?
	jnz    .LerrorS6

	chi    %r3,0x10				# read info successful?
	je     .LgotinfoS6

	ahi    %r7,4				# next try
	j      .LretryS6

.LgotinfoS6:
	lr     %r2,%r6
	j      .LendS6

.LerrorS6:
	lhi    %r2,0				# failure

.LendS6:
	exit_linkage

	br     %r14

.LreadinfoS6:
	.long  0x00120001
	.long  0x00020001
	.long  0
.LinfosccbS6:
	.word  0x1000
	.byte  0
	.byte  0,0
	.byte  0
	.word  0
.LinfosccbendS6:

	# Global constants
.LsccbS0:
	.long  SCCB_ADDR

.endm # sclp_read_info

.macro sclp_param

#
# Subroutine to read load parameter.
#
# Parameters:
#   R2  = destination address for load parameter (8 bytes)
#
# Returns:
#   R2  = 0 on success, 1 on failure
#

_sclp_param:
	stm    %r6,%r15,24(%r15)                # save registers

	basr   %r13,0                           # get base register
.LbaseS7:
	ahi    %r15,-96                         # create stack frame

	lr     %r8,%r2                          # save dest address
	bras   %r14,_sclp_read_info
	chi    %r2,0
	je     .LerrorS7

	mvc    0(8,%r8),SCCB_PARAM(%r2)         # copy parameter to dest address
	sr     %r2,%r2
	j      .LendS7
.LerrorS7:
	lhi    %r2,1                            # failure
.LendS7:
	lm     %r6,%r15,120(%r15)               # restore registers

	br     %r14

.endm # sclp_param

.macro sclp_hsa_copy_64

#
# Function which initializes HSA copy.
#
# Has to be called from 64 bit code.
#
# Parameters:
#   R2  = destination address for temp page
#
_sclp_hsa_copy_init_64:
	entry_linkage
	basr   %r13,0				# get base register
0:
	lgr    %r10,%r2				# save r2

	lghi   %r2,0				# with receive mask = 0
	llgf   %r3,.LsclpsendmaskS8-0b(%r13)	# init sclp mask
	bras   %r14,_sclp_set_mask

	lghi   %r2,0
	bras   %r14,_sclp_setup

	llgf   %r9,.LsccbS0-0b(%r13)		# get sccb address
	lghi   %r1,0				# init event status with zero
	st     %r1,SDIAS_EVSTATE(%r9)

	lgr    %r2,%r10				# destination (temp page)
	lghi   %r3,2				# copy first
	lghi   %r4,1				# memory block
	bras   %r14,_sclp_hsa_copy_64		# from HSA

	lghi   %r1,0
	ic     %r1,SDIAS_EVSTATE(%r9)		# get event status
	ltr    %r1,%r1				# event status still zero?
	jnz    .LexitS8				# no: use sync mode

	lghi   %r10,0
	st     %r10,.LusesyncS9-0b(%r13)	# yes: use async mode
	lghi   %r2,1				# disable sclp
	bras   %r14,_sclp_setup			#
	llgf   %r2,.LsclprecmaskS8-0b(%r13)	# init sclp mask
	llgf   %r3,.LsclpsendmaskS8-0b(%r13)	# with SDIAS receive mask = 1
	bras   %r14,_sclp_set_mask		# and enable
	lghi   %r2,0				# sclp again
	bras   %r14,_sclp_setup
.LexitS8:
	exit_linkage
	br     %r14
.LsclprecmaskS8:
	.long 0x00000010
.LsclpsendmaskS8:
	.long 0x40000010

_sclp_hsa_copy_exit_64:
	entry_linkage
	basr   %r13,0				# get base register
0:
	lghi   %r2,1				# cleanup SCLP
	bras   %r14,_sclp_setup

	lghi   %r2,0				# release HSA
	lghi   %r3,2
	diag   %r2,%r3,0x308

	exit_linkage
	br     %r14

#
# Function which copies from HSA to absolute storage (SDIAS).
#
# Has to be called from 64 bit code.
#
# Parameters:
#   R2  = destination address
#   R3  = number of first block to copy (header at block 1, lowcore at block 2)
#   R4  = total number of blocks to copy
#
# Returns:
#   R2  = 0 on success, 1 on failure
#

_sclp_hsa_copy_64:
	entry_linkage
	basr   %r13,0				# get base register
0:
	llgf   %r8,.LsccbS0-0b(%r13)		# get sccb address
	mvc    0(.LendsccbS9-.LwritesccbS9,%r8),.LwritesccbS9-0b(%r13)
	stg    %r2,.LdestS9-.LwritesccbS9(%r8)	# store destination (64 bit)
	st     %r3,.LstartblkS9-.LwritesccbS9(%r8)# store start block
	st     %r4,.LblkcntS9-.LwritesccbS9(%r8)# store block count
	llgf   %r2,.LwritedataS9-0b(%r13)	# write data
	lgr    %r3,%r8
	bras   %r14,_sclp_servc
	ltr    %r2,%r2				# servc successful?
	jnz    .LerrorS9
	chi    %r3,0x20				# write data successful?
	jne    .LerrorS9

	icm     %r7,15,.LusesyncS9-0b(%r13)	# if sync mode, exit
	jnz    .LchecksccbS9

	tml    %r4,0x3				# event buffer pending?
	jnz    .LgeteventbufS9
	lghi   %r2,0
	bras   %r14,_sclp_wait_int		# no, wait for unsol. interrupt
	tml    %r3,0x3				# event buffer pending?
	jz     .LerrorS9			# no, something went wrong
.LgeteventbufS9:
	bras   %r14,_sclp_hsa_copy_wait
	ltr    %r2,%r2				# successful?
	jnz    .LerrorS9			# no, exit

.LchecksccbS9:
	lghi   %r2,0				# set exit code
	llgf   %r9,.LsccbS0-0b(%r13)		# get sccb address
	lghi   %r1,0
	ic     %r1,SDIAS_EVSTATE(%r9)		# get event status
	chi    %r1,SDIAS_EVSTATE_PART_STORED	# part stored?
	je     .LendS9				# yes, exit
	chi    %r1,SDIAS_EVSTATE_ALL_STORED	# all stored (last junk)?
	je     .LendS9				# yes, exit
.LerrorS9:
	lghi   %r2,1				# error: load return code
.LendS9:
	exit_linkage
	br     %r14
.align 8
.LusesyncS9:
	.long  0x00000001			# flag for synchronous mode
.LwritedataS9:
	.long  0x00760005			# SCLP command for SDIAS
.LwritesccbS9:
	# sccb
	.word  .LendsccbS9-.LwritesccbS9
	.byte  0
	.byte  0,0,0
	.word  0
	# evbuf
.LevbufS9:
	.word  .LendsccbS9-.LevbufS9	# length
	.byte  0x1C			# type
	.byte  0			# flags
	.word  0			# _reserved
	.byte  0			# event_qual
	.byte  0			# data_id
	.quad  0			# reserved2
	.long  0x4712			# event_id
	.word  0			# reserved3
#ifdef USE_64BIT_SCLP
	.byte  1			# asa_size
#else
	.byte  0			# asa_size
#endif
	.byte  0			# event_status
	.long  0			# reserved4
.LblkcntS9:
	.long  0			# blk_cnt
.LdestS9:
	.quad  0			# asa
	.long  0			# reserved5
.LstartblkS9:
	.long  0			# fbn
	.long  0			# reserved6
	.long  0			# lbn
	.word  0			# reserved7
	.word  1			# dbs
.LendsccbS9:

#
# Subroutine which reads event buffer (result of SDIAS)
#
# Returns:
#   R2  = 0 ok, 1 on failure, 2 on timeout
#

_sclp_hsa_copy_wait:
	entry_linkage
	basr   %r13,0				# get base register
0:
	llgf   %r3,.LsccbS0-0b(%r13)		# get sccb address

	mvc    0(.LreadevbufS10-.LreadsccbS10,%r3),.LreadsccbS10-0b(%r13)
	l      %r2,.LreaddataS10-0b(%r13)	# read event data
	bras   %r14,_sclp_servc

	ltr    %r2,%r2				# servc successful?
	jnz    .LexitfailS10			# no, exit fail

	chi    %r3,0x20				# read event data successful?
	je     .LexitokS10			# yes, exit ok

	lghi   %r2,1				# no, exit fail
	j      .LexitfailS10			#

.LexitokS10:
	lghi   %r2,0
.LexitfailS10:
	exit_linkage
	br     %r14

.LreaddataS10:
	.long  0x00770005			# SCLP command: read event data
.LreadsccbS10:
	.word  0x1000
	.byte  0
	.byte  0,0,0
	.word  0
.LreadevbufS10:
.endm # sclp_hsa_copy

.macro entry_linkage

#
# sclp subroutines called by dump tools operating in 64-bit mode need
# to set bits 0-31 of working registers to zero. Otherwise the sclp code,
# which was originally designed to operate in 31-bit mode, might fail.
# Depending on 31 or 64-bit mode this entry linkage
# - saves the caller's registers r6-r15 with stm/stmg
# - nulls the stack frame and loads working registers r6-r13 with 0

#ifdef USE_64BIT_SCLP
	stmg   %r6,%r15,48(%r15)                # save registers
	ahi    %r15,-200                        # create stack frame
	xc     0(200,%r15),0(%r15)              # init stack frame with 0
	lmg    %r6,%r13,0(%r15)                 # zero work registers
#else
	stm    %r6,%r15,24(%r15)                # save registers
	ahi    %r15,-96                         # create stack frame
#endif

.endm entry_linkage

.macro exit_linkage

#ifdef USE_64BIT_SCLP
	lmg    %r6,%r15,248(%r15)               # restore registers
#else
	lm     %r6,%r15,120(%r15)               # restore registers
#endif

.endm exit_linkage
