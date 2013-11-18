# Common definitions
include ../../common.mak

CPPFLAGS += -I../include -I../boot -I../../include \
	    -DZFCPDUMP_DIR=$(ZFCPDUMP_DIR) \
	    -DZFCPDUMP_IMAGE=$(ZFCPDUMP_IMAGE) -DZFCPDUMP_RD=$(ZFCPDUMP_RD) \
	    -D_FILE_OFFSET_BITS=64
LDFLAGS += -Wl,-z,noexecstack
objects = misc.o proc.o error.o scan.o job.o boot.o bootmap.o disk.o \
	  install.o zipl.o ../boot/data.o
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

boot.o: ../boot/data.o ../boot/data.h
