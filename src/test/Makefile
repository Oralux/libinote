TARGET = text2tlv tlv2text
CC = gcc
CFLAGS += $(DEBUG) -I../api -std=c11
LDFLAGS += -L $(DESTDIR)/lib
LDLIBS = -linote
DESTDIR ?= ../../build/x86_64/usr/

text2tlv:	text2tlv.o
	$(CC) -o $(@) $(^) $(LDFLAGS) -L $(DESTDIR)/lib -linote

tlv2text:	tlv2text.o
	$(CC) -o $(@) $(^) $(LDFLAGS) -L $(DESTDIR)/lib -linote

all: $(TARGET)

clean:
	rm -f *o *~ $(TARGET)

install:
	echo none
#mkdir -p $(DESTDIR)/bin; install -D -m 755 $(TARGET) inote.sh $(DESTDIR)/bin
