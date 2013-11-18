include ../common.mak

CPPFLAGS += -I../include

all: qethqoat

qethqoat.o: qethqoat.c qethqoat.h

qethqoat: qethqoat.o

install: all
	$(INSTALL) -d -m 755 $(BINDIR) $(MANDIR)/man8
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 755 qethqoat $(BINDIR)
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 qethqoat.8 $(MANDIR)/man8

clean:
	rm -f *.o *~ qethqoat core

.PHONY: all install clean
