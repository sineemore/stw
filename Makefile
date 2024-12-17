.POSIX:

include config.mk

stw: config.h arg.h stw.o
	$(CC) $(STWLDFLAGS) -o stw stw.o

.c.o:
	$(CC) $(STWCFLAGS) -c $<

config.h:
	cp config.def.h config.h

install: stw
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f stw $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/stw
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	cp stw.1 $(DESTDIR)$(MANPREFIX)/man1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/stw.1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/stw\
		$(DESTDIR)$(MANPREFIX)/man1/stw.1

clean:
	rm -f stw stw.o

_format:
	clang-format -i stw.c

_bear:
	bear -- make -B stw
