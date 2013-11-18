include ../common.mak

CPPFLAGS += -I../include

all: qetharp

qetharp.o: qeth26.h qetharp.h

qetharp: qetharp.o

install: all
	$(INSTALL) -d -m 755 $(MANDIR)/man8 $(BINDIR)
	$(INSTALL) -m 755 qetharp $(BINDIR)
	$(INSTALL) -m 644 qetharp.8 $(MANDIR)/man8 

clean:
	rm -f qetharp *.o *~ core

.PHONY: all install clean
