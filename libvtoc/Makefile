include ../common.mak

CPPFLAGS += -I../include

all: vtoc.o

vtoc.o: vtoc.c ../include/vtoc.h

install: all

clean:
	rm -f *.o *~ core
