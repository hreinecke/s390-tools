include ../common.mak

CPPFLAGS += -I../include

all: mon_fsstatd mon_procd

mon_fsstatd.o: mon_fsstatd.h
mon_fsstatd: mon_fsstatd.o

mon_procd.o: mon_procd.h
mon_procd: mon_procd.o

install: all
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 755 mon_fsstatd $(USRSBINDIR)
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 mon_fsstatd.8  $(MANDIR)/man8
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 755 mon_procd $(USRSBINDIR)
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 mon_procd.8 $(MANDIR)/man8

clean:
	rm -f *.o *~ mon_fsstatd mon_procd core

.PHONY: all install clean
