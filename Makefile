.POSIX:

include config.mk

stw: stw.o arg.h config.h
	$(CC) $(STWLDFLAGS) -o $@ stw.o

.c.o:
	$(CC) $(STWCFLAGS) -c $<

_format:
	clang-format -i stw.c

_bear:
	bear -- make -B stw

clean:
	rm -f stw stw.o

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f stw $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/stw
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	cp stw.1 $(DESTDIR)$(MANPREFIX)/man1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/stw.1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/stw\
		$(DESTDIR)$(MANPREFIX)/man1/stw.1