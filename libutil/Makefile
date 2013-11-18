include ../common.mak

CPPFLAGS += -I../include

all: util_list.o

util_list.o: util_list.c ../include/util.h

install: all

clean:
	rm -f *.o *~ core
