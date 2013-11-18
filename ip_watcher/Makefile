include ../common.mak

CPPFLAGS += -I../include
all: xcec-bridge

xcec-bridge: xcec-bridge.o

clean:
	rm -f *.o core xcec-bridge

install: ip_watcher.pl xcec-bridge start_hsnc.sh
	@cat start_hsnc.sh | \
	sed -e 's+%S390_TOOLS_VERSION%+$(S390_TOOLS_RELEASE)+' \
	>$(USRSBINDIR)/start_hsnc.sh; \
	chown $(OWNER).$(GROUP) $(USRSBINDIR)/start_hsnc.sh; \
	chmod 755 $(USRSBINDIR)/start_hsnc.sh; \
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 755 ip_watcher.pl $(USRSBINDIR)
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 755 xcec-bridge $(USRSBINDIR)

.PHONY: all install clean
