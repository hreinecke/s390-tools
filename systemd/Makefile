include ../common.mak

SYSTEM_UNITS = ttyrun-getty@.service iucvtty-login@.service

all:

system_units: $(SYSTEM_UNITS)

check:

install: system_units
	for unit in $(SYSTEM_UNITS); do \
	  test -d "$(SYSTEMDSYSTEMUNITDIR)" || continue ; \
	  $(INSTALL) -g $(GROUP) -o $(OWNER) \
	             -m 644 $$unit $(SYSTEMDSYSTEMUNITDIR) ; \
	done

clean:
	rm -f $(SYSTEM_UNITS)

%: %.in
	real_bin_dir=`echo $(BINDIR) |sed 's#^$(INSTROOT)##'` ; \
	real_usrbin_dir=`echo $(USRBINDIR) |sed 's#^$(INSTROOT)##'` ; \
	real_usrsbin_dir=`echo $(USRSBINDIR) |sed 's#^$(INSTROOT)##'` ; \
	sed    -e "s#@bin_path@#$$real_bin_dir#g" \
	       -e "s#@usrbin_path@#$$real_usrbin_dir#g" \
	       -e "s#@usrsbin_path@#$$real_usrsbin_dir#g" \
	       -e 's#@S390_TOOLS_RELEASE@#$(S390_TOOLS_RELEASE)#g' \
	< $< > $@

.PHONY: all check install clean system_units
