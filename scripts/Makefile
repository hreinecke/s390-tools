include ../common.mak

SCRIPTS	= dbginfo.sh zfcpdbf scsi_logging_level
MAN_PAGES = dbginfo.sh.1 zfcpdbf.1

all:

install:
	$(INSTALL) -d -m 755 $(BINDIR) $(MANDIR)/man1
	@for i in $(SCRIPTS); \
	do \
		cat $$i | \
		sed -e 's+%S390_TOOLS_VERSION%+$(S390_TOOLS_RELEASE)+' \
		>$(BINDIR)/$$i; \
		chown $(OWNER):$(GROUP) $(BINDIR)/$$i; \
		chmod 755 $(BINDIR)/$$i; \
	done
	
	@for i in $(MAN_PAGES); \
	do \
		$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 $$i \
		$(MANDIR)/man1; \
	done

clean:

.PHONY: all install clean
