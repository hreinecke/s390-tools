ifndef $(COMMON_INCLUDED)
COMMON_INCLUDED = true

# Global definitions
# The variable "DISTRELEASE" should be overwritten in rpm spec files with:
# "make DISTRELEASE=%{release}" and "make install DISTRELEASE=%{release}"
VERSION            = 1
RELEASE            = 24
PATCHLEVEL         = 1
DISTRELEASE        = build-$(shell date +%Y%m%d)
S390_TOOLS_RELEASE = $(VERSION).$(RELEASE).$(PATCHLEVEL)-$(DISTRELEASE)
export S390_TOOLS_RELEASE

reldir = $(subst $(shell cd -P $(dir $(filter %common.mak,$(MAKEFILE_LIST))); \
	 pwd)/,,$(CURDIR))
rootdir= $(dir $(filter %common.mak,$(MAKEFILE_LIST)))
export S390_TEST_LIB_PATH=$(rootdir)/s390-tools-testsuite/lib

# Cross Compiling Support
CROSS_COMPILE   =
AS              = $(call echocmd,"  AS      ",/$@)$(CROSS_COMPILE)as
LINK            = $(call echocmd,"  LINK    ",/$@)$(CROSS_COMPILE)gcc
LD              = $(call echocmd,"  LD      ",/$@)$(CROSS_COMPILE)ld
CC              = $(call echocmd,"  CC      ",/$@)$(CROSS_COMPILE)gcc
LINKXX          = $(call echocmd,"  LINKXX  ",/$@)$(CROSS_COMPILE)g++
CXX             = $(call echocmd,"  CXX     ",/$@)$(CROSS_COMPILE)g++
CPP             = $(call echocmd,"  CPP     ",/$@)$(CROSS_COMPILE)gcc -E
AR              = $(call echocmd,"  AR      ",/$@)$(CROSS_COMPILE)ar
NM              = $(call echocmd,"  NM      ",/$@)$(CROSS_COMPILE)nm
STRIP           = $(call echocmd,"  STRIP   ",/$@)$(CROSS_COMPILE)strip
OBJCOPY         = $(call echocmd,"  OBJCOPY ",/$@)$(CROSS_COMPILE)objcopy
OBJDUMP         = $(call echocmd,"  OBJDUMP ",/$@)$(CROSS_COMPILE)objdump
RUNTEST         = $(call echocmd,"  RUNTEST ",/$@)$(S390_TEST_LIB_PATH)/s390_runtest
CHECK           = sparse
CHECKTOOL       = $(call echocmd,"  CHECK   ",/$@)$(CHECK)

INSTALL         = install
CP              = cp
ifneq ("${V}","1")
	MAKEFLAGS += --quiet
	echocmd=echo $1$(call reldir)$2;
	RUNTEST += > /dev/null 2>&1
else
	echocmd=
endif
ifeq ("${W}","1")
	WARNFLAGS = -W -Wall -Wextra
else
	WARNFLAGS = -W -Wall
endif
# Support alternate install root
INSTROOT        =
USRSBINDIR      = $(INSTROOT)/usr/sbin
USRBINDIR       = $(INSTROOT)/usr/bin
BINDIR          = $(INSTROOT)/sbin
LIBDIR          = $(INSTROOT)/lib
SYSCONFDIR      = $(INSTROOT)/etc
MANDIR          = $(INSTROOT)/usr/share/man
TOOLS_LIBDIR    = $(INSTROOT)/lib/s390-tools
ZFCPDUMP_DIR    = $(INSTROOT)/lib/s390-tools/zfcpdump
# Systemd support files are installed only if a directory is specified
# for SYSTEMDSYSTEMUNITDIR
SYSTEMDSYSTEMUNITDIR =
INSTDIRS        = $(USRSBINDIR) $(USRBINDIR) $(BINDIR) $(LIBDIR) $(MANDIR) \
			$(SYSCONFDIR) $(TOOLS_LIBDIR) $(ZFCPDUMP_DIR) \
			$(SYSTEMDSYSTEMUNITDIR)
OWNER           = $(shell id -un)
GROUP		= $(shell id -gn)
export INSTROOT BINDIR LIBDIR MANDIR OWNER GROUP

# Special defines for zfcpdump
ZFCPDUMP_PART_IMAGE	= zfcpdump_part.image
ZFCPDUMP_PART_RD	= zfcpdump_part.rd
export ZFCPDUMP_DIR ZFCPDUMP_PART_IMAGE ZFCPDUMP_PART_RD

CFLAGS		= $(WARNFLAGS) -O3 -DS390_TOOLS_RELEASE=$(S390_TOOLS_RELEASE) \
			-DS390_TOOLS_LIBDIR=$(TOOLS_LIBDIR) \
			-DS390_TOOLS_SYSCONFDIR=$(SYSCONFDIR) \
			-g $(OPT_FLAGS)
CXXFLAGS	= $(WARNFLAGS) -O3 -DS390_TOOLS_RELEASE=$(S390_TOOLS_RELEASE) \
			-DS390_TOOLS_LIBDIR=$(TOOLS_LIBDIR) \
			-DS390_TOOLS_SYSCONFDIR=$(SYSCONFDIR) \
			 -g $(OPT_FLAGS)

# make G=1
# Compile tools so that gcov can be used to collect code coverage data.
# See the gcov man page for details.
ifeq ("${G}","1")
CFLAGS := $(filter-out -O%,$(CFLAGS)) --coverage
CXXFLAGS := $(filter-out -O%,$(CXXFLAGS)) --coverage
LDFLAGS += --coverage
endif
export AS LD CC CPP AR NM STRIP OBJCOPY OBJDUMP INSTALL CFLAGS CXXFLAGS LDFLAGS

# Overwrite implicite makefile rules for having nice compile output
%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
ifeq ("${C}","1")
	$(CHECKTOOL) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
endif

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

%: %.o
	$(LINK) $(LDFLAGS) $^ $(LOADLIBES) $(LDLIBS) -o $@

all:

# rules for shared files
# needed to ensure that these files are build with their own build
# flags even if they are build from external directories
$(rootdir)/libutil/util_part.o:
	make -C $(rootdir)/libutil/ util_part.o

$(rootdir)/libutil/util_list.o:
	make -C $(rootdir)/libutil/ util_list.o

$(rootdir)/libutil/util_proc.o:
	make -C $(rootdir)/libutil/ util_proc.o

$(rootdir)/libvtoc/vtoc.o:
	make -C $(rootdir)/libvtoc/ vtoc.o

$(rootdir)/zipl/boot/data.o:
	make -C $(rootdir)/zipl/boot/ data.o

install_dirs:
	for dir in $(INSTDIRS); do \
		test -d $$dir || $(INSTALL) -g $(GROUP) -o $(OWNER) -d $$dir; \
	done
	for i in 1 2 3 4 5 6 7 8; do \
		test -d $(MANDIR)/man$$i || $(INSTALL) -g $(GROUP) -o $(OWNER) \
		-d $(MANDIR)/man$$i; \
	done

install_echo:
	$(call echocmd,"  INSTALL ")

install: install_echo install_dirs

clean_echo:
	$(call echocmd,"  CLEAN   ")
clean_gcov:
	rm -f *.gcda *.gcno *.gcov

clean: clean_echo clean_gcov
endif

