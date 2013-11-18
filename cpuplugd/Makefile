include ../common.mak

CFLAGS += -I../include

all: cpuplugd

LDLIBS += -lm

OBJECTS = daemon.o cpu.o info.o terms.o config.o main.o getopt.o mem.o
$(OBJECTS): cpuplugd.h

cpuplugd: $(OBJECTS)
	$(LINK) $(LDFLAGS) $^ $(LOADLIBES) $(LDLIBS) -o $@

clean:
	rm -f cpuplugd $(OBJECTS)

install: all
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 755 cpuplugd $(USRSBINDIR)
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 man/cpuplugd.8 $(MANDIR)/man8
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 644 man/cpuplugd.conf.5 \
		$(MANDIR)/man5

.PHONY: all install clean
