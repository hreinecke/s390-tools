include ../common.mak

CPPFLAGS += -D_FILE_OFFSET_BITS=64 -I../include -I/usr/include/fuse
LDLIBS += -lz

all: zgetdump

OBJECTS = zgetdump.o opts.o zg.o \
	  dfi.o dfi_vmcoreinfo.o \
	  dfi_lkcd.o dfi_elf.o dfi_s390.o dfi_s390mv.o dfi_s390tape.o \
	  dfi_kdump.o dfi_devmem.o \
	  dfo.o dfo_elf.o dfo_s390.o \
	  df_s390.o \
	  dt.o dt_s390sv.o dt_s390mv.o \
	  stdout.o

ifneq ("$(WITHOUT_FUSE)","1")
LDLIBS += -lfuse
OBJECTS += zfuse.o
else
CPPFLAGS += -DWITHOUT_FUSE
endif

$(OBJECTS): *.h Makefile

zgetdump: $(OBJECTS) ../libutil/util_list.o

install: all
	$(INSTALL) -d -m 755 $(MANDIR)/man8 $(BINDIR)
	$(INSTALL) -m 755 zgetdump $(BINDIR)
	$(INSTALL) -m 644 zgetdump.8 $(MANDIR)/man8

clean:
	rm -f *.o *~ zgetdump core.*

.PHONY: all install clean
