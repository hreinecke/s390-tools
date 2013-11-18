include ../common.mak

CPPFLAGS += -I../include

all: vmcp

vmcp.o: vmcp.c vmcp.h ../include/zt_common.h

vmcp: vmcp.o

install: all
	$(INSTALL) -d -m 755 $(BINDIR) $(MANDIR)/man8
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 755 vmcp $(BINDIR)
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 vmcp.8 $(MANDIR)/man8 

clean:
	rm -f *.o *~ vmcp core

.PHONY: all install clean
