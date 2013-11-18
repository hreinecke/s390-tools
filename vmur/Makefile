# Common definitions
include ../common.mak

CPPFLAGS += -D_FILE_OFFSET_BITS=64 -I../include -I../vmconvert
LDLIBS += -lz
VMCONVERT_SRC	= ../vmconvert/convert.cpp ../vmconvert/lkcd_dump.cpp \
		  ../vmconvert/vm_dump.cpp ../vmconvert/register_content.cpp \
		  ../vmconvert/dump.cpp ../vmconvert/dump.h \
		  ../vmconvert/lkcd_dump.h ../vmconvert/register_content.h \
		  ../vmconvert/vm_dump.h
VMCONVERT_OBJS	= ../vmconvert/convert.o ../vmconvert/lkcd_dump.o \
		  ../vmconvert/vm_dump.o ../vmconvert/register_content.o \
		  ../vmconvert/dump.o

OBJS = vmur.o $(VMCONVERT_OBJS)

all: vmur

vmur.o: vmur.cpp vmur.h $(VMCONVERT_SRC)

vmur: $(OBJS)
	$(LINKXX) $(LDFLAGS) $^ $(LOADLIBES) $(LDLIBS) -o $@

install: all
	$(INSTALL) -d -m 755 $(USRSBINDIR) $(MANDIR)/man8
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 755 vmur $(USRSBINDIR)
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 vmur.8 \
		$(MANDIR)/man8

clean:
	rm -f *.o *~ vmur

.PHONY: all install clean
