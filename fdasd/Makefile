include ../common.mak

CPPFLAGS += -I../include

all: fdasd

fdasd.o: fdasd.h ../include/zt_common.h

fdasd: fdasd.o ../libvtoc/vtoc.o

install: all
	$(INSTALL) -d -m 755 $(BINDIR) $(MANDIR)/man8
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 755 fdasd $(BINDIR)
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 fdasd.8  $(MANDIR)/man8 

clean:
	rm -f *.o *~ fdasd core

.PHONY: all install clean
