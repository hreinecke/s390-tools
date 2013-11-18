#
# Toplevel Makefile for zfcpdump
#
# Copyright IBM Corp. 2003, 2006.
# Author(s): Michael Holzheu <holzheu@de.ibm.com>
#

-include ../common.mak
-include ./config

ARCH := $(shell uname -m | sed -e s/i.86/i386/ -e s/sun4u/sparc64/ -e s/arm.*/arm/ -e s/sa110/arm/)

all: subdirs_make

SUB_DIRS = kernel ramdisk

subdirs_make:
	/bin/sh ./get_files.sh -c; if [ "$$?" != "0" ] ; then exit ; fi; \
	for dir in $(SUB_DIRS) ; do \
		( cd $$dir && \
			$(MAKE) ARCH=$(ARCH) ) \
		|| exit $?; \
	done

clean:
	for dir in $(SUB_DIRS) ; do \
		( cd $$dir && \
			$(MAKE) ARCH=$(ARCH) clean ) \
		|| exit $$?; \
        done
 
install:
	/bin/sh ./get_files.sh -c; if [ "$$?" != "0" ] ; then exit ; fi; \
	if [ ! -d $(INSTROOT)/$(ZFCPDUMP_DIR) ]; then install -d $(INSTROOT)/$(ZFCPDUMP_DIR) -m 775; fi ; \
	for dir in $(SUB_DIRS) ; do \
		(cd $$dir && $(MAKE) ARCH=$(ARCH) install) \
		|| exit $$?; \
	done
