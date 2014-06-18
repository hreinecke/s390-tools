include ../common.mak

CPPFLAGS += -I../include -DSYSFS
CFLAGS += -D_FILE_OFFSET_BITS=64 -DHAVE_SETXATTR -I/usr/include/fuse -pthread
LDLIBS += -lfuse -lpthread -lrt -ldl -lm -lzds
LDFLAGS += -L../libzds

all: zdsfs

zdsfs: zdsfs.o

zdsfs.o: ../include/zt_common.h ../include/libzds.h

install: all
	$(INSTALL) -d -m 755 $(USRBINDIR) $(MANDIR)/man1
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 755 zdsfs $(USRBINDIR)
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 zdsfs.1  $(MANDIR)/man1

clean:
	rm -f *.o *~ zdsfs core

.PHONY: all install clean
