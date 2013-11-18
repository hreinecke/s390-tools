#!/usr/bin/make -f

include ../common.mak

CPPFLAGS += -I../include

all: cmsfs-fuse

CFLAGS += -D_FILE_OFFSET_BITS=64 -DHAVE_SETXATTR -I/usr/include/fuse
LDLIBS += -lfuse -lpthread -lrt -ldl -lm

OBJECTS = cmsfs-fuse.o dasd.o amap.o config.o
$(OBJECTS): *.h Makefile 

CMSFS_FUSE_DIR = $(SYSCONFDIR)/cmsfs-fuse
CONFIG_FILES = filetypes.conf

cmsfs-fuse: $(OBJECTS) ../libutil/util_list.o

install: all
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 755 cmsfs-fuse $(USRBINDIR)
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 cmsfs-fuse.1 $(MANDIR)/man1
	$(INSTALL) -g $(GROUP) -o $(OWNER) -d $(CMSFS_FUSE_DIR)
	for cnf in $(CONFIG_FILES); do \
	  $(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 etc/$$cnf $(CMSFS_FUSE_DIR) ; \
	done

clean:
	rm -f cmsfs-fuse *.o

.PHONY: all install clean
