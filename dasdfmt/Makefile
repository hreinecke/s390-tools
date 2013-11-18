include ../common.mak

CPPFLAGS += -I../include

all: dasdfmt

dasdfmt.o: dasdfmt.h ../include/zt_common.h

dasdfmt: dasdfmt.o ../libvtoc/vtoc.o

install: all
	$(INSTALL) -d -m 755 $(BINDIR) $(MANDIR)/man8
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 755 dasdfmt $(BINDIR)
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 dasdfmt.8  $(MANDIR)/man8 

clean:
	rm -f *.o *~ dasdfmt core

.PHONY: all install clean
