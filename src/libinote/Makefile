LIB = libinote.a
BIN = lib.o debug.o 
#CFLAGS += $(DEBUG) -I. -I../api -Wall -std=c99 -pedantic
CFLAGS += $(DEBUG) -I. -I../api
CC = gcc
DESTDIR ?= ../../build/x86_64/usr/

all: $(BIN)
	$(AR) rcs $(LIB) $(^) 

clean:
	rm -f *o *~ $(LIB) $(DESTDIR)/lib/$(LIB) $(DESTDIR)/include/inote.h

install:
	install -D -m 644  $(LIB) $(DESTDIR)/lib/$(LIB)
	install -D ../api/inote.h $(DESTDIR)/include/inote.h
