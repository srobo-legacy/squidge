PKGS := "gtk+-2.0 gdk-2.0 gmodule-2.0 gdk-pixbuf-2.0"
CFLAGS  += `pkg-config --cflags $(PKGS)` -ggdb3 -Wall -O3
LDFLAGS += `pkg-config --libs $(PKGS)`

all: squidge

squidge: squidge.c squidge-gtkbuilder.c squidge-splash-img.c camview.c
	$(CC) $^ -o $@ $(CFLAGS) $(LDFLAGS)

squidge-gtkbuilder.c: squidge.gtkbuilder convert-gtkbuild
	./convert-gtkbuild $< $@

squidge-splash-img.c: splash.png convert-splash
	./convert-splash $< squidge-splash-img

install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install squidge $(DESTDIR)$(PREFIX)/bin/squidge

clean:
	-rm -f *.o squidge squidge-gtkbuilder.c
	-rm -f squidge-splash-img.[hc]

.PHONY: clean
