TARGET = text2tlv tlv2text
CC = gcc
CFLAGS += $(DEBUG) -I../api -std=c99
LDFLAGS = -L $(DESTDIR)/lib
LDLIBS = -linote
DESTDIR ?= ../../build/x86_64/usr/

text2tlv:	text2tlv.o
	$(CC) -o $(@) $(^) -L $(DESTDIR)/lib -linote

tlv2text:	tlv2text.o
	$(CC) -o $(@) $(^) -L $(DESTDIR)/lib -linote

all: $(TARGET)

clean:
	rm -f *o *~ $(TARGET)

install:
	mkdir -p $(DESTDIR)/bin; install -D -m 755 $(TARGET) $(DESTDIR)/bin
