include ../common.mak
include Makefile.rules

all: osasnmpd

osasnmpd: $(OBJS)

install: all
	$(INSTALL) -d -m 755 $(USRSBINDIR) $(MANDIR)/man8
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 755 osasnmpd $(USRSBINDIR)
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 osasnmpd.8  $(MANDIR)/man8

clean:
	rm -f $(OBJS) osasnmpd core

.PHONY: all install clean
