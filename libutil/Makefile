include ../common.mak

CPPFLAGS += -I../include

all: util_list.o util_part.o util_proc.o

util_list.o: util_list.c ../include/util.h

util_proc.o: util_proc.c ../include/util_proc.h

install: all

clean:
	rm -f *.o *~ core
