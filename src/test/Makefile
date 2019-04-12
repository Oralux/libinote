CFLAGS += $(DEBUG) -I../api -std=c99
CC = gcc
DESTDIR ?= ../../build/x86_64/usr/

inote:	inote.o
	$(CC) -o $(@) $(^) -L $(DESTDIR)/lib -linote

all:	inote

clean:
	rm -f inote *o *~

install:
	install -d $(DESTDIR)/bin
	install -m 755 inote inote.sh $(DESTDIR)/bin

