include ../common.mak

CPPFLAGS += -I../include -DSYSFS

all: dasdview

dasdview.o: dasdview.h ../include/zt_common.h

dasdview: dasdview.o ../libvtoc/vtoc.o ../libu2s/u2s.o

install: all
	$(INSTALL) -d -m 755 $(BINDIR) $(MANDIR)/man8
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 755 dasdview $(BINDIR)
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 dasdview.8  $(MANDIR)/man8 

clean:
	rm -f *.o *~ dasdview core

.PHONY: all install clean
