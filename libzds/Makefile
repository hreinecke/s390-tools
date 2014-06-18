include ../common.mak

CPPFLAGS += -I../include
CFLAGS += -D_FILE_OFFSET_BITS=64

%.a: %.o
	$(AR) rcs $@ $^

all: libzds.a

libzds.a: libzds.o $(rootdir)/libutil/util_list.o $(rootdir)/libvtoc/vtoc.o

libzds.o: ../include/libzds.h

util_list.o: util.h

install: all

clean:
	rm -f *.o *.a *~ core

.PHONY: all install clean
