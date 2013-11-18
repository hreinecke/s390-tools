include ../common.mak

CPPFLAGS += -I../include

all: u2s.o

u2s.o: u2s.c ../include/u2s.h

install: all

clean:
	rm -f *.o *~ core
