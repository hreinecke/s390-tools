include ../../common.mak

CPPFLAGS += -I../include -I../boot -I../../include
includes = $(wildcard ../include/*.h)

all: tunedasd

objects = tunedasd.o disk.o
$(objects): $(includes)

tunedasd: $(objects)

install: all
	$(INSTALL) -d -m 755 $(BINDIR)
	$(INSTALL) -g $(GROUP) -o $(OWNER) -m 755 tunedasd $(BINDIR)

clean:
	rm -f *.o tunedasd

.PHONY: all install clean
