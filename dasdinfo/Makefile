include ../common.mak

CPPFLAGS += -I../include

all: dasdinfo

dasdinfo: dasdinfo.o ../libu2s/u2s.o

install: all
	$(INSTALL) -d -m 755 $(BINDIR) $(MANDIR)/man8
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 755 dasdinfo $(BINDIR)
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 dasdinfo.8  $(MANDIR)/man8

clean:
	rm -f *.o *~ dasdinfo core

.PHONY: all install clean
