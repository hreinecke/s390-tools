include ../common.mak

CPPFLAGS += -D_FILE_OFFSET_BITS=64 -I../include
LDLIBS += -lz

all: vmconvert

lkcd_dump.o: lkcd_dump.cpp lkcd_dump.h
vm_dump.o: vm_dump.cpp vm_dump.h
dump.o: dump.cpp dump.h
register_content.o: register_content.cpp register_content.h
convert.o: convert.cpp vm_dump.h lkcd_dump.h

vmconvert: main.o lkcd_dump.o vm_dump.o register_content.o dump.o convert.o
	$(LINKXX) $(LDFLAGS) $^ $(LOADLIBES) $(LDLIBS) -o $@

install: all
	$(INSTALL) -d -m 755 $(USRBINDIR) $(MANDIR)/man8
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 755 vmconvert $(USRBINDIR)
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 vmconvert.8  $(MANDIR)/man8

clean:
	rm -f *.o *~ vmconvert core

.PHONY: all install clean
