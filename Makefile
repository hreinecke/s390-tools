ARCH := $(shell uname -m | sed -e s/i.86/i386/ -e s/sun4u/sparc64/ -e s/arm.*/arm/ -e s/sa110/arm/)

# Include commond definitions
include common.mak

LIB_DIRS = libvtoc libu2s libutil
SUB_DIRS = $(LIB_DIRS) zipl zdump fdasd dasdfmt dasdview tunedasd \
	   tape390 osasnmpd qetharp ip_watcher qethconf scripts zconf \
	   vmconvert vmcp man mon_tools dasdinfo vmur cpuplugd ipl_tools \
	   ziomon iucvterm hyptop cmsfs-fuse qethqoat

all: subdirs_make

subdirs_make:
	set -e ; for dir in $(SUB_DIRS) ; do \
		cd $$dir ; $(MAKE) TOPDIR=$(TOPDIR) ARCH=$(ARCH); cd ..; \
	done

clean:
	set -e ; for dir in $(SUB_DIRS) ; do \
		cd $$dir ; $(MAKE) TOPDIR=$(TOPDIR) ARCH=$(ARCH) clean; cd ..; \
	done

install:
	set -e ; for dir in $(SUB_DIRS) ; do \
		cd $$dir ; $(MAKE) TOPDIR=$(TOPDIR) ARCH=$(ARCH) install; cd ..; \
	done
