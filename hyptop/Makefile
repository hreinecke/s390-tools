include ../common.mak

CPPFLAGS += -I../include

all: hyptop

LDLIBS += -lncurses

OBJECTS = hyptop.o opts.o helper.o \
	  sd_core.o sd_sys_items.o sd_cpu_items.o \
	  tbox.o table.o table_col_unit.o \
	  dg_debugfs.o dg_debugfs_lpar.o dg_debugfs_vm.o \
	  win_sys_list.o win_sys.o win_fields.o \
	  win_cpu_types.o win_help.o nav_desc.o

$(OBJECTS): *.h Makefile

hyptop: $(OBJECTS) ../libutil/util_list.o

install: all
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 755 hyptop $(USRSBINDIR)
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 hyptop.8  $(MANDIR)/man8

clean:
	rm -f *.o *~ hyptop core

.PHONY: all install clean
