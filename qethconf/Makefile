include ../common.mak

all:

install: qethconf
	cat qethconf | \
	sed -e 's+%S390_TOOLS_VERSION%+$(S390_TOOLS_RELEASE)+' \
	>$(BINDIR)/qethconf; \
	chown $(OWNER).$(GROUP) $(BINDIR)/qethconf; \
	chmod 755 $(BINDIR)/qethconf; \
	$(INSTALL) -d -m 755 $(BINDIR) $(MANDIR)/man8
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 qethconf.8  $(MANDIR)/man8

clean:

.PHONY: all install clean
