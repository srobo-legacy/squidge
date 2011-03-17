CFLAGS  += `pkg-config --cflags gtk+-2.0 gdk-2.0 gmodule-2.0` -ggdb3
LDFLAGS += `pkg-config --libs gtk+-2.0 gdk-2.0 gmodule-2.0`

all: squidge

squidge: squidge.c squidge-gtkbuilder.c
	$(CC) $^ -o $@ $(CFLAGS) $(LDFLAGS)

squidge-gtkbuilder.c: squidge.gtkbuilder convert-gtkbuild
	./convert-gtkbuild $< $@

install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install squidge $(DESTDIR)$(PREFIX)/bin/squidge

clean:
	-rm -f *.o squidge squidge-gtkbuilder.c

.PHONY: clean
