include ../common.mak

CPPFLAGS += -D_FILE_OFFSET_BITS=64 -I../include

all: tape390_display tape390_crypt

tape390_common.o: tape390_common.h

tape390_display.o: tape390_common.h
tape390_display: tape390_display.o tape390_common.o

tape390_crypt.o: tape390_common.h
tape390_crypt: tape390_crypt.o tape390_common.o

install: all
	$(INSTALL) -d -m 755 $(BINDIR) $(MANDIR)/man8
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 755 tape390_display $(BINDIR)
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 tape390_display.8 \
		$(MANDIR)/man8
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 755 tape390_crypt $(BINDIR)
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 tape390_crypt.8 \
		$(MANDIR)/man8

clean:
	rm -f *.o *~ tape390_display tape390_crypt core

.PHONY: all install clean
