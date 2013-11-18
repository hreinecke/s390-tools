include ../common.mak

CPPFLAGS += -I../include

all: chreipl lsreipl chshut lsshut

objects = main.o ccw.o fcp.o system.o shutdown.o \
	  cmd_lsshut.o cmd_chshut.o cmd_lsreipl.o cmd_chreipl.o proc.o
$(objects): ipl_tools.h

chreipl: $(objects)
	$(LINK) $(LDFLAGS) $^ $(LOADLIBES) $(LDLIBS) -o $@

lsreipl:
	ln -sf chreipl lsreipl

chshut:
	ln -sf chreipl chshut

lsshut:
	ln -sf chreipl lsshut

clean:
	rm -f *.o lsreipl chreipl chshut lsshut

install: all
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 755 chreipl $(USRSBINDIR)
	ln -f -s chreipl $(USRSBINDIR)/lsreipl
	ln -f -s chreipl $(USRSBINDIR)/chshut
	ln -f -s chreipl $(USRSBINDIR)/lsshut
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 man/chreipl.8 $(MANDIR)/man8
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 man/lsreipl.8 $(MANDIR)/man8
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 man/lsshut.8 $(MANDIR)/man8
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 man/chshut.8 $(MANDIR)/man8

.PHONY: all install clean

