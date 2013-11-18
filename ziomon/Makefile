include ../common.mak

CPPFLAGS += -I../include -DNDEBUG
CFLAGS   += -Wundef -Wstrict-prototypes -Wno-trigraphs
CXXFLAGS += -Wundef -Wno-trigraphs

TARGETS = ziomon_util ziomon_mgr ziomon_zfcpdd ziorep_utilization ziorep_traffic
all: $(TARGETS)

ziomon_mgr_main.o: ziomon_mgr.c
	$(CC) -DWITH_MAIN $(CFLAGS) $(CPPFLAGS) -c $< -o $@
ziomon_mgr: LDLIBS += -lm
ziomon_mgr: ziomon_dacc.o ziomon_util.o ziomon_mgr_main.o ziomon_tools.o \
	    ziomon_zfcpdd.o ziomon_msg_tools.o
	$(LINK) $(LDFLAGS) $^ $(LOADLIBES) $(LDLIBS) -o $@

ziomon_util_main.o: ziomon_util.c ziomon_util.h
	$(CC) -DWITH_MAIN $(CFLAGS) $(CPPFLAGS) -c $< -o $@
ziomon_util: LDLIBS += -lm
ziomon_util: ziomon_util_main.o ziomon_tools.o
	$(LINK) $(LDFLAGS) $^ $(LOADLIBES) $(LDLIBS) -o $@

ziomon_zfcpdd_main.o: ziomon_zfcpdd.c ziomon_zfcpdd.h
	$(CC) -DWITH_MAIN $(CFLAGS) $(CPPFLAGS) -c $< -o $@
ziomon_zfcpdd: LDLIBS += -lm -lrt -lpthread
ziomon_zfcpdd: ziomon_zfcpdd_main.o ziomon_tools.o
	$(LINK) $(LDFLAGS) $^ $(LOADLIBES) $(LDLIBS) -o $@

ziorep_traffic: ziorep_traffic.o ziorep_framer.o ziorep_frameset.o \
		ziorep_printers.o ziomon_dacc.o ziomon_util.o \
		ziomon_msg_tools.o ziomon_tools.o ziomon_zfcpdd.o \
		ziorep_cfgreader.o ziorep_collapser.o ziorep_utils.o \
		ziorep_filters.o
	$(LINKXX) $(LDFLAGS) $^ $(LOADLIBES) $(LDLIBS) -o $@

ziorep_utilization: ziorep_utilization.o ziorep_framer.o ziorep_frameset.o \
		    ziorep_printers.o ziomon_dacc.o ziomon_util.o \
		    ziomon_msg_tools.o ziomon_tools.o ziomon_zfcpdd.o \
		    ziorep_cfgreader.o ziorep_collapser.o ziorep_utils.o \
		    ziorep_filters.o
	$(LINKXX) $(LDFLAGS) $^ $(LOADLIBES) $(LDLIBS) -o $@

install: all
	cat ziomon  | sed -e 's/%S390_TOOLS_VERSION%/$(S390_TOOLS_RELEASE)/' \
		> $(USRSBINDIR)/ziomon;
	chown $(OWNER).$(GROUP) $(USRSBINDIR)/ziomon;
	chmod 755 $(USRSBINDIR)/ziomon;
	cat ziomon_fcpconf  | sed -e \
		's/%S390_TOOLS_VERSION%/$(S390_TOOLS_RELEASE)/' \
		> $(USRSBINDIR)/ziomon_fcpconf;
	chown $(OWNER).$(GROUP) $(USRSBINDIR)/ziomon_fcpconf;
	chmod 755 $(USRSBINDIR)/ziomon_fcpconf;
	cat ziorep_config  | sed -e \
		's/%S390_TOOLS_VERSION%/$(S390_TOOLS_RELEASE)/' > \
		$(USRSBINDIR)/ziorep_config;
	chown $(OWNER).$(GROUP) $(USRSBINDIR)/ziorep_config;
	chmod 755 $(USRSBINDIR)/ziorep_config;
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 ziomon.8 $(MANDIR)/man8
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 ziomon_fcpconf.8 \
		$(MANDIR)/man8
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 755 ziomon_util $(USRSBINDIR)
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 ziomon_util.8 $(MANDIR)/man8
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 755 ziomon_mgr $(USRSBINDIR)
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 ziomon_mgr.8 $(MANDIR)/man8
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 755 ziomon_zfcpdd $(USRSBINDIR)
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 ziomon_zfcpdd.8 $(MANDIR)/man8
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 ziorep_config.8 $(MANDIR)/man8
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 755 ziorep_utilization \
		$(USRSBINDIR)
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 ziorep_utilization.8 \
		$(MANDIR)/man8
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 755 ziorep_traffic $(USRSBINDIR)
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 ziorep_traffic.8 \
		$(MANDIR)/man8

uninstall:
	rm $(USRSBINDIR)/ziomon
	rm $(USRSBINDIR)/ziomon_util
	rm $(USRSBINDIR)/ziomon_mgr
	rm $(USRSBINDIR)/ziomon_zfcpdd
	rm $(USRSBINDIR)/ziomon_fcpconf
	rm $(USRSBINDIR)/ziorep_config
	rm $(USRSBINDIR)/ziorep_utilization
	rm $(USRSBINDIR)/ziorep_traffic
	rm $(MANDIR)/man8/ziomon.8*
	rm $(MANDIR)/man8/ziomon_util.8*
	rm $(MANDIR)/man8/ziomon_mgr.8*
	rm $(MANDIR)/man8/ziomon_zfcpdd.8*
	rm $(MANDIR)/man8/ziomon_fcpconf.8*
	rm $(MANDIR)/man8/ziorep_config.8*
	rm $(MANDIR)/man8/ziorep_utilization.8*
	rm $(MANDIR)/man8/ziorep_traffic.8*

clean:
	-rm -f *.o $(TARGETS)
