#
# Makefile to build kernel part of zfcpdump
#
# Copyright IBM Corp. 2003, 2006.
# Author(s): Michael Holzheu <holzheu@de.ibm.com>
#

-include ../../common.mak
-include ../config

ARCH := $(shell uname -m | sed -e s/i.86/i386/ -e s/sun4u/sparc64/ -e s/arm.*/arm/ -e s/sa110/arm/)

KERNEL=../extern/$(KERNEL_TARBALL)

all: $(ZFCPDUMP_IMAGE)

$(ZFCPDUMP_IMAGE): $(KERNEL) dump.h dump.c
	/bin/sh ./create_kernel.sh $(ARCH)

install: $(ZFCPDUMP_IMAGE)
	/bin/sh ./create_kernel.sh -i

clean clobber:
	/bin/sh ./create_kernel.sh -r
