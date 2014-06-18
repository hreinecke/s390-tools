include ../common.mak

CPPFLAGS += -I../include

CPIOINIT  = $(call echocmd,"  CPIOINI ",/$@)./cpioinit

all: $(ZFCPDUMP_PART_RD)

cpioinit: cpioinit.c
zfcpdump.o: zfcpdump.h
zfcpdump_part.o: zfcpdump.h

zfcpdump_part: zfcpdump.o zfcpdump_part.o
	$(LINK) $(LDFLAGS) $^ -static -o $@
	$(STRIP) -s $@

$(ZFCPDUMP_PART_RD): cpioinit zfcpdump_part
	$(CPIOINIT) zfcpdump_part | gzip > $(ZFCPDUMP_PART_RD)

install: all
	$(INSTALL) -m 611 $(ZFCPDUMP_PART_RD) $(ZFCPDUMP_DIR)

clean:
	rm -f *.o *~ zfcpdump_part cpioinit $(ZFCPDUMP_PART_RD)

.PHONY: all clean install
