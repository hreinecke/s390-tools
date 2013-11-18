# Common definitions
include ../common.mak

ARCH := $(shell uname -m)
LINUX_VERSION := 2.6.27
LINUX_DIR := linux-$(LINUX_VERSION)
E2FSPROGS := e2fsprogs-1.41.3

CFLAGS        += -D_FILE_OFFSET_BITS=64

all: zfcpdump.image

zfcpdump: zfcpdump.c zfcpdump.h
	$(CC) $(CFLAGS) -D GZIP_SUPPORT -static -o $@ zfcpdump.c -lz

e2fsck:
	tar xfzv $(E2FSPROGS).tar.gz
	cd $(E2FSPROGS); ./configure; make
	cp $(E2FSPROGS)/e2fsck/e2fsck e2fsck

zfcpdump.image: zfcpdump e2fsck
	tar xfjv linux-$(LINUX_VERSION).tar.bz2
	cp kernel-$(LINUX_VERSION)-zfcpdump.$(ARCH).config $(LINUX_DIR)/.config
	cp zfcpdump_initramfs.txt $(LINUX_DIR)/arch/s390/boot
	cp e2fsck $(LINUX_DIR)/arch/s390/boot
	cp zfcpdump $(LINUX_DIR)/arch/s390/boot
	patch -p0 < linux-2.6.27-zfcp-scan-online.patch
	cd $(LINUX_DIR); make oldconfig; make image
	cp $(LINUX_DIR)/arch/s390/boot/image zfcpdump.image

install: all
	$(INSTALL) -d $(INSTROOT)/$(ZFCPDUMP_DIR) -m 775
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 zfcpdump.image \
		$(INSTROOT)/$(ZFCPDUMP_DIR)

clean:
	rm -f *.o *~ zfcpdump zfcpdump.image e2fsck
	rm -rf $(LINUX_DIR) $(E2FSPROGS)

.PHONY: all clean install
