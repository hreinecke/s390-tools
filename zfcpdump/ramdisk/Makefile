#
# Makefile to build user space (ramdisk) part of zfcpdump
#
# Copyright IBM Corp. 2003, 2006.
# Author(s): Michael Holzheu <holzheu@de.ibm.com>
#

-include ../../common.mak
-include ../config

ARCH := $(shell uname -m | sed -e s/i.86/i386/ -e s/sun4u/sparc64/ -e s/arm.*/arm/ -e s/sa110/arm/)

all: $(ZFCPDUMP_RD)

CFLAGS += -D_FILE_OFFSET_BITS=64

$(ZFCPDUMP_RD): zfcp_dumper
	/bin/sh ./create_rd.sh $(ARCH)

zfcp_dumper: zfcp_dumper.o
	$(CC) -o zfcp_dumper -static zfcp_dumper.o -lz

zfcp_dumper.o: zfcp_dumper.c zfcp_dumper.h
	$(CC) $(CFLAGS) -c -I../../include zfcp_dumper.c

install: $(ZFCPDUMP_RD)
	/bin/sh ./create_rd.sh -i

clean clobber:
	/bin/sh ./create_rd.sh -r
	rm -f *.o

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<
