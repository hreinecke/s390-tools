# Common definitions
include ../../common.mak

CPPFLAGS += -I../include -I../boot -I../../include \
	    -DZFCPDUMP_DIR=$(ZFCPDUMP_DIR) \
	    -DZFCPDUMP_FS_IMAGE=$(ZFCPDUMP_FS_IMAGE) \
	    -DZFCPDUMP_FS_RD=$(ZFCPDUMP_FS_RD) \
	    -DZFCPDUMP_PART_IMAGE=$(ZFCPDUMP_PART_IMAGE) \
	    -DZFCPDUMP_PART_RD=$(ZFCPDUMP_PART_RD) \
	    -D_FILE_OFFSET_BITS=64
LDFLAGS += -Wl,-z,noexecstack

objects = misc.o error.o scan.o job.o boot.o bootmap.o disk.o \
	  install.o zipl.o $(rootdir)/zipl/boot/data.o \
	  $(rootdir)/libutil/util_part.o $(rootdir)/libutil/util_proc.o

includes = $(wildcard ../include/*.h)
zipl_helpers = $(wildcard zipl_helper.*)
chreipl_helpers = $(subst zipl_,chreipl_, $(zipl_helpers))

all: zipl $(chreipl_helpers)

zipl: $(objects)

$(objects): $(includes)

chreipl_helper.%: zipl_helper.%
	ln -s $< $@

install: all
	$(INSTALL) -d -m 755 $(BINDIR)
	$(INSTALL) -c zipl $(BINDIR)
	$(INSTALL) -m 755 $(zipl_helpers) $(chreipl_helpers) $(TOOLS_LIBDIR)
	$(CP) --no-dereference $(chreipl_helpers) $(TOOLS_LIBDIR)

clean:
	rm -f *.o $(chreipl_helpers) zipl

.PHONY: all install clean

# Additional manual dependencies
boot.o: ../boot/data.h

../boot/data.h:
	make -C ../boot data.h

../boot/data.o:
	make -C ../boot data.o

../../libutil/util_part.o:
	make -C ../../libutil util_part.o
