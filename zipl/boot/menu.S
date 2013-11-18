#
# s390-tools/zipl/boot/menu.S
#   Interactive boot menu.
#
# Copyright IBM Corp. 2004, 2006.
#
# Author(s): Peter Oberparleiter (Peter.Oberparleiter@de.ibm.com)
#


LC_CPUID		= 0xC60			# low core addres for cpu id

MENU_PARM_LENGTH	= 896			# max length of parmline
MENU_PARMLINE		= 0xA000-0x400		# address for additional params
MENU_TEMP_AREA		= 0x5000		# address for temp storage

MENU_PARAM_FLAG		= 0			# offset of flag parameter
MENU_PARAM_TIMEOUT	= 2			# offset of timeout parameter
MENU_PARAM_BANNER	= 4			# offset of banner parameter
MENU_PARAM_CONFIG	= 6			# offset of config parameter
MENU_PARAM_CONFIG_KDUMP	= 132			# offset of kdump parameter

#include "sclp.S"

	sclp_base
	sclp_print
	sclp_read
	sclp_read_info
	sclp_param

#
# Subroutine which implements an interactive boot menu.
#
# Returns:
#   R2  = configuration number to boot
#

_menu:
	stm    %r6,%r15,24(%r15)		# save registers
	stidp  LC_CPUID				# need CPUID for VM check

	basr   %r13,0				# get base register
.LbaseM1:
	ahi    %r15,-96				# create stack frame

	l      %r6,.LparmareaM0-.LbaseM1(%r13)	# get menu param area address

	sr     %r8,%r8				# default config = 0

	l      %r7,.LparmlineM0-.LbaseM1(%r13)	# get address for parmline
	xc     0(256,%r7),0(%r7)		# clear parmline area
	xc     256(256,%r7),256(%r7)
	xc     512(256,%r7),512(%r7)
	xc     768(256,%r7),768(%r7)

	sr     %r2,%r2				# initialize sclp
	bras   %r14,_sclp_setup
	ltr    %r2,%r2				# init succeeded?
	jnz    .LendM1

	bras   %r14,_menu_param			# get loadparm

	ltr    %r2,%r2				# got number?
	jnz    .LnonumberM1

	lr     %r8,%r3				# save configuration number
	j      .LendmenuM1

.LnonumberM1:
	chi    %r2,1				# got keyword?
	jne    .LnokeywordM1

	ltr    %r3,%r3				# keyword 'PROMPT'?
	jz     .LdopromptM1

.LnokeywordM1:
	lh     %r1,MENU_PARAM_FLAG(%r6)
	ltr    %r1,%r1				# is menu enabled?
	jz     .LendmenuM1			# nope, skip it

.LdopromptM1:
	lh     %r2,MENU_PARAM_BANNER(%r6)	# print banner
	ar     %r2,%r6
	bras   %r14,_sclp_print

	bras   %r14,_menu_list			# print config list

	tm     LC_CPUID,0xff			# check for VM
        jno    .LnovmnoteM1

	la     %r2,.Ltext1M0-.LbaseM1(%r13)	# print VM note
	bras   %r14,_sclp_print

.LnovmnoteM1:
	lr     %r2,%r6				# print menu prompt
	bras   %r14,_menu_read
	lr     %r8,%r2				# save menu result

.LendmenuM1:
	chi    %r8,0				# make sure number is correct
	jl     .LnumnotokM1

	chi    %r8,62
	jle    .LnumokM1

.LnumnotokM1:
	lpsw   .LpswM1-.LbaseM1(%r13)		# indicate invalid config

.LnumokM1:
	l      %r2,.LtempareaM0-.LbaseM1(%r13)	# print config name
	mvc    0(.Ltext5M0-.Ltext2M0,%r2),.Ltext2M0-.LbaseM1(%r13)

	la     %r3,.Ltext4M0-.Ltext2M0(%r2)
	la     %r4,MENU_PARAM_CONFIG(%r6)	# address of first config text

	lr     %r0,%r8				# get offset for config
	sll    %r0,1
	ar     %r4,%r0

	lh     %r4,0(%r4)			# get address
	ltr    %r4,%r4				# valid configuration?
	jz     .LnumnotokM1			# no

	ar     %r4,%r6
	lr     %r9,%r4				# save number string address
	ahi    %r4,4				# skip number string

	sr     %r0,%r0				# temp = 0
	sr     %r1,%r1				# index = 0
.LcopyloopM1:
	ic     %r0,0(%r1,%r4)			# copy config name
	stc    %r0,0(%r1,%r3)
	ltr    %r0,%r0
	jz     .LendloopM1
	ahi    %r1,1
	j      .LcopyloopM1

.LendloopM1:
	ar     %r3,%r1
	mvc    0(.Ltext5M0-.Ltext4M0,%r3),.Ltext4M0-.LbaseM1(%r13)

.LprintconfM1:
	bras   %r14,_sclp_print

	# append "BOOT_IMAGE=<num>" to parameter line

	sr     %r0,%r0
	sr     %r1,%r1
	lr     %r2,%r7				# address of extra parmline
	lhi    %r3,MENU_PARM_LENGTH

.LfindendM1:
	ic     %r0,0(%r1,%r2)
	ltr    %r0,%r0
	jz     .LfoundendM1
	ahi    %r1,1
	brct   %r3,.LfindendM1
	j      .LcleanupM1

.LfoundendM1:
	chi    %r1,MENU_PARM_LENGTH-(.Ltext12M0-.Ltext11M0+3)
	jhe    .LcleanupM1			# parmline too long

	ar     %r2,%r1				# append " BOOT_IMAGE="
	mvc    0(.Ltext12M0-.Ltext11M0,%r2),.Ltext11M0-.LbaseM1(%r13) 
	ahi    %r2,.Ltext12M0-.Ltext11M0

	ic     %r0,0(%r9)			# skip leading blank
	chi    %r0,0x40
	je     .LblankM1

	stc    %r0,0(%r2)			# store first digit
	ahi    %r2,1

.LblankM1:
	mvc    0(1,%r2),1(%r9)			# store second digit
	ahi    %r2,1

	xc     0(1,%r2),0(%r2)			# append null byte

.LcleanupM1:
	la     %r2,1				# cleanup sclp interface
	bras   %r14,_sclp_setup

.LendM1:
	lr     %r2,%r8				# get menu result
	lm     %r6,%r15,120(%r15)		# restore registers

	br     %r14

	.align 8
.LpswM1:
	.long  0x000a0000,0x00000300		# PSW to indicate invalid config


#
# Subroutine which prints out the list of available boot configurations.
#

_menu_list:
	stm    %r6,%r15,24(%r15)		# save registers

	basr   %r13,0				# get base register
.LbaseM2:
	ahi    %r15,-96				# create stack frame

	l      %r8,.LparmareaM0-.LbaseM2(%r13)	# get menu param area address
	la     %r6,63				# number of configs
	la     %r7,MENU_PARAM_CONFIG(%r8)	# address of first config text
	sr     %r9,%r9

.LloopM2:
	lh     %r2,0(%r9,%r7)			# get offset for config name
	ltr    %r2,%r2				# no config name?
	jz     .LemptyconfigM2			# skip

	ar     %r2,%r8				# print config name
	bras   %r14,_sclp_print

	ltr    %r9,%r9				# first config?
	jnz    .LemptyconfigM2			# no, skip

	la     %r2,.Ltext5M0-.LbaseM2(%r13)	# print empty line
	bras   %r14,_sclp_print

.LemptyconfigM2:
	ahi    %r9,2				# increase config name offset
	brct   %r6,.LloopM2

	la     %r2,.Ltext5M0-.LbaseM2(%r13)	# print empty line
	bras   %r14,_sclp_print

	lm     %r6,%r15,120(%r15)		# restore registers

	br     %r14


#
# Subroutine which prints out the boot prompt.
#

_menu_prompt:
	stm    %r6,%r15,24(%r15)		# save registers

	basr   %r13,0				# get base register
.LbaseM3:
	ahi    %r15,-96				# create stack frame

	l      %r2,.LparmareaM0-.LbaseM3(%r13)	# get menu param area address
	lh     %r0,MENU_PARAM_TIMEOUT(%r2)	# get timeout value

	l      %r3,.LtempareaM0-.LbaseM3(%r13)	# get temp area
	lr     %r2,%r3				# save temp area

	xc     0(.Ltext10M0-.Ltext6M0+16,%r3),0(%r3)	# clear temp area

	# "Please choose..."
	mvc    0(.Ltext7M0-.Ltext6M0,%r3),.Ltext6M0-.LbaseM3(%r13)
	ahi    %r3,.Ltext7M0-.Ltext6M0

	ltr    %r0,%r0				# timeout set?
	jz     .LnotimeoutM3			# no, skip

	# "default will.."
	mvc    0(.Ltext8M0-.Ltext7M0,%r3),.Ltext7M0-.LbaseM3(%r13)
	ahi    %r3,.Ltext8M0-.Ltext7M0

	cvd    %r0,.LdecimalM3-.LbaseM3(%r13)	# convert to EBCDIC number
	unpk   0(16,%r3),.LdecimalM3-.LbaseM3(8,%r13)

	sr     %r0,%r0
.LloopzeroesM3:
	ic     %r0,0(%r3)			# remove leading zeroes

	chi    %r0,0xf0				# leading zero?
	jne    .LloopdigitsM3

	mvc    0(16,%r3),1(%r3)			# remove zero
	j      .LloopzeroesM3

.LloopdigitsM3:
	ic     %r0,0(%r3)			# skip digits

	ltr    %r0,%r0				# end of string?
	jz     .LendloopdigitsM3

	oi     0(%r3),0xf0			# overwrite sign zone

	ahi    %r3,1
	j      .LloopdigitsM3

.LendloopdigitsM3:
	# " seconds"
	mvc    0(.Ltext9M0-.Ltext8M0,%r3),.Ltext8M0-.LbaseM3(%r13)
	ahi    %r3,.Ltext9M0-.Ltext8M0

.LnotimeoutM3:
	# ":"
	mvc    0(.Ltext10M0-.Ltext9M0,%r3),.Ltext9M0-.LbaseM3(%r13)

	bras   %r14,_sclp_print			# print prompt

	lm     %r6,%r15,120(%r15)		# restore registers

	br     %r14

.LdecimalM3:
	.quad  0


#
# Subroutine which reads and interprets user input.
#
# Returns:
#   R2  = configuration number to boot
#

_menu_read:
	stm    %r6,%r15,24(%r15)		# save registers

	basr   %r13,0				# get base register
.LbaseM4:
	ahi    %r15,-96				# create stack frame

	l      %r6,.LparmareaM0-.LbaseM4(%r13)	# get menu param area address
	l      %r7,.LtempareaM0-.LbaseM4(%r13)	# get temp area = input area

.LpromptM4:
	bras   %r14,_menu_prompt		# print menu prompt

	lh     %r2,MENU_PARAM_TIMEOUT(%r6)	# get timeout value
	lr     %r3,%r7
	bras   %r14,_sclp_read

	ltr    %r2,%r2				# got input?
	jnz    .LusedefaultM4

	sth    %r2,MENU_PARAM_TIMEOUT(%r6)	# deactivate timeout

	ltr    %r8,%r3				# save input length
	jz     .LpromptM4

	tm     LC_CPUID,0xff			# VM?
	jno    .LnotolowerM4			# no, skip

	lr     %r2,%r7				# convert input to lower case
	bras   %r14,_menu_tolower

.LnotolowerM4:
	lr     %r2,%r7				# get number
	lr     %r3,%r8
	bras   %r14,_menu_etoi

	ltr    %r9,%r4				# save and test valid flag
	jnz    .LnoconfigM4

	ltr    %r1,%r5				# check for valid configuration
	jl     .LnoconfigM4			# negative value?

	chi    %r1,62				# too high?
	jh     .LnoconfigM4

	sll    %r1,1				# get offset into menu param
	lh     %r1,MENU_PARAM_CONFIG(%r1,%r6)	# get text offset
	ltr    %r1,%r1				# available?
	jz     .LnoconfigM4

	ltr    %r3,%r3				# check for additional params
	jz     .LnoaddparamM4

	sr     %r0,%r0
	l      %r4,.LparmlineM0-.LbaseM4(%r13)	# get address for parmline

.LcopyaddparamM4:
	ic     %r0,0(%r2)			# copy rest of parmline
	stc    %r0,0(%r4)
	ahi    %r2,1
	ahi    %r4,1
	brct   %r3,.LcopyaddparamM4

.LnoaddparamM4:
	lr     %r2,%r5
	j      .LendM4

.LnoconfigM4:
	la     %r2,.Ltext10M0-.LbaseM4(%r13)	# "Error: undefined config.."
	bras   %r14,_sclp_print

	j      .LpromptM4

.LusedefaultM4:
	sr     %r2,%r2				# set default configuration

.LendM4:
	lm     %r6,%r15,120(%r15)		# restore registers

	br     %r14


#
# Subroutine to convert an EBCDIC string to all lower case.
#
# Parameters:
#   R2  = address of EBCDIC string
#   R3  = length of string
#

_menu_tolower:
	sr     %r0,%r0

.LloopM5:
	ic     %r0,0(%r2)			# convert input to lowercase

	chi    %r0,0xc1				# 'A' - 'I'
	jl     .LnoupperM5

	chi    %r0,0xc9
	jle    .LtolowerM5

	chi    %r0,0xd1				# 'J' - 'R'
	jl     .LnoupperM5

	chi    %r0,0xd9
	jle    .LtolowerM5

	chi    %r0,0xe2				# 'S' - 'Z'
	jl     .LnoupperM5

	chi    %r0,0xe9
	jh     .LnoupperM5

.LtolowerM5:
	ni     0(%r2),0xbf			# remove upper case bit

.LnoupperM5:
	ahi    %r2,1				# next char
	brct   %r3,.LloopM5

	br     %r14


#
# Subroutine to parse an EBCDIC string as a decimal integer number.
#
# Parameters:
#   R2  = address of EBCDIC string
#   R3  = length of string
#
# Returns:
#   R2  = address of rest of EBCDIC string after number
#   R3  = remaining length of string after number
#   R4  = 0 if valid number was found, non-zero otherwise
#   R5  = number if R4 = 0
#

_menu_etoi:
	sr     %r0,%r0
	lhi    %r4,1				# non-success return code
	sr     %r5,%r5				# resulting number

.LskipspaceM6:
	ic     %r0,0(%r2)			# skip leading spaces

	chi    %r0,0x40				# is current char a space?
	jne    .LafterspaceM6

	ahi    %r2,1				# skip spaces
	brct   %r3,.LskipspaceM6

	j      .LendM6				# reached end of string

.LafterspaceM6:
	lhi    %r1,0x0f

.LtonumberM6:
	ic     %r0,0(%r2)

	chi    %r0,0x40				# got a space?
	je     .LgotnumberM6			# valid end of number separator

	chi    %r0,0xf0				# got a digit?
	jl     .LendM6

	chi    %r0,0xf9
	jh     .LendM6

	mhi    %r5,10				# current number * 10

	nr     %r0,%r1				# add digit
	ar     %r5,%r0

	ahi    %r2,1				# next character
	brct   %r3,.LtonumberM6			# until end of input string
 
.LgotnumberM6:
	sr     %r4,%r4
 
.LendM6:
	br     %r14


#
# Subroutine used to interpret load parameter.
#
# Returns:
#   R2  = 0 if a number was found, 1 if a keyword was found, 2 otherwise
#   R3  = number value if R2=0, keyword number if R2=1
#

_menu_param:
	stm    %r6,%r15,24(%r15)		# save registers

	basr   %r13,0				# get base register
.LbaseM7:
	ahi    %r15,-96				# create stack frame

	la     %r6,.LparamM7-.LbaseM7(%r13)	# parameter area

	lr     %r2,%r6				# retrieve load parameter
	bras   %r14,_sclp_param

	ltr    %r2,%r2				# success?
	jnz    .LerrorM7

	lr     %r2,%r6				# try to parse number
	lhi    %r3,8
	bras   %r14,_menu_etoi

	ltr    %r2,%r4				# got number?
	lr     %r3,%r5
	jz     .LendM7

	lhi    %r1,8				# remove trailing spaces
	la     %r2,7(%r6)

.LremblanksM7:
	cli    0(%r2),0x40			# got space?
	jne    .LafterblanksM7

	xc     0(1,%r2),0(%r2)			# write zero

	ahi    %r2,-1				# check next char
	brct   %r1,.LremblanksM7

.LafterblanksM7:
	lr     %r2,%r6				# convert loadparm to lowercase
	lhi    %r3,8
	bras   %r14,_menu_tolower

	# check for keyword
	sr     %r0,%r0				# set ending char to '\0'
	lr     %r2,%r6				# get param address
	la     %r3,.LkeypromptM7-.LbaseM7(%r13)	# get keyword address
	clst   %r2,%r3				# compare strings until '\0'
	brc    7,.LerrorM7			# not equal

	lhi    %r2,1				# found keyword
	sr     %r3,%r3

	j      .LendM7

.LerrorM7:
	lhi    %r2,2				# failure

.LendM7:
	lm     %r6,%r15,120(%r15)		# restore registers

	br     %r14

.LparamM7:
	.byte  0,0,0,0,0,0,0,0			# room for the load parameter

.LkeypromptM7:
	# 'prompt\0'
	.byte  0x97, 0x99, 0x96, 0x94, 0x97, 0xa3, 0x00
	.align 2


	# Global constants

.LparmareaM0:
	.long  .Lmenuparam
.LparmlineM0:
	.long  MENU_PARMLINE
.LtempareaM0:
	.long  MENU_TEMP_AREA


	# Menu text

.Ltext1M0:
	# "Note: VM users please use '#cp vi vmsg <input>'\n"
	.byte  0xd5, 0x96, 0xa3, 0x85, 0x7a, 0x40, 0xe5, 0xd4, 0x40, 0xa4
	.byte  0xa2, 0x85, 0x99, 0xa2, 0x40, 0x97, 0x93, 0x85, 0x81, 0xa2
	.byte  0x85, 0x40, 0xa4, 0xa2, 0x85, 0x40, 0x7d, 0x7b, 0x83, 0x97
	.byte  0x40, 0xa5, 0x89, 0x40, 0xa5, 0x94, 0xa2, 0x87, 0x40, 0x4c
	.byte  0x89, 0x95, 0x97, 0xa4, 0xa3, 0x6e, 0x7d, 0x15, 0x40, 0x00
.Ltext2M0:
	# "Booting "
	.byte  0xc2, 0x96, 0x96, 0xa3, 0x89, 0x95, 0x87, 0x40
.Ltext4M0:
	# "..."
	.byte  0x4b, 0x4b, 0x4b, 0x00
.Ltext5M0:
	# "\n"
	.byte  0x40, 0x00
.Ltext6M0:
	# "Please choose"
	.byte  0xd7, 0x93, 0x85, 0x81, 0xa2, 0x85, 0x40, 0x83, 0x88, 0x96
	.byte  0x96, 0xa2, 0x85
.Ltext7M0:
	# " (default will boot in "
	.byte  0x40, 0x4d, 0x84, 0x85, 0x86, 0x81, 0xa4, 0x93, 0xa3, 0x40
	.byte  0xa6, 0x89, 0x93, 0x93, 0x40, 0x82, 0x96, 0x96, 0xa3, 0x40
	.byte  0x89, 0x95, 0x40
.Ltext8M0:
	# " seconds)"
	.byte  0x40, 0xa2, 0x85, 0x83, 0x96, 0x95, 0x84, 0xa2, 0x5d
.Ltext9M0:
	# ":"
	.byte  0x7a, 0x00
.Ltext10M0:
	# "Error: undefined configuration\n"
	.byte  0xc5, 0x99, 0x99, 0x96, 0x99, 0x7a, 0x40, 0xa4, 0x95, 0x84
	.byte  0x85, 0x86, 0x89, 0x95, 0x85, 0x84, 0x40, 0x83, 0x96, 0x95
	.byte  0x86, 0x89, 0x87, 0xa4, 0x99, 0x81, 0xa3, 0x89, 0x96, 0x95
	.byte  0x15, 0x40, 0x00
.Ltext11M0:
	# " BOOT_IMAGE="
	.byte  0x40, 0xc2, 0xd6, 0xd6, 0xe3, 0x6d, 0xc9, 0xd4, 0xc1, 0xc7
	.byte  0xc5, 0x7e
.Ltext12M0:

	.align 2

	.macro menu_param_area
	.align 8
.Lmenuparam:
	.endm

