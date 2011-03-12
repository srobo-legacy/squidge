CFLAGS  += `pkg-config --cflags gtk+-2.0 gdk-2.0` -ggdb3
LDFLAGS += `pkg-config --libs gtk+-2.0 gdk-2.0`

all: squidge

squidge: squidge.c
	$(CC) $^ -o $@ $(CFLAGS) $(LDFLAGS)

install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install squidge $(DESTDIR)$(PREFIX)/bin/squidge

clean:
	-rm -f *.o squidge

.PHONY: clean
