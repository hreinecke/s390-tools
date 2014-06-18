include ../common.mak

CPPFLAGS += -I../include -DSYSFS
LDLIBS += -lzds
LDFLAGS += -L../libzds

all: dasdview

dasdview.o: dasdview.h ../include/zt_common.h ../include/libzds.h

dasdview: dasdview.o ../libu2s/u2s.o

install: all
	$(INSTALL) -d -m 755 $(BINDIR) $(MANDIR)/man8
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 755 dasdview $(BINDIR)
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 dasdview.8  $(MANDIR)/man8 

clean:
	rm -f *.o *~ dasdview core

.PHONY: all install clean
