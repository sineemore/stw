.POSIX:
.SUFFIXES:
PREFIX ?= /usr/local
MANPREFIX = $(PREFIX)/share/man
NAME    = stw
CC      = cc
INCS    = -I/usr/include/X11 -I/usr/include/freetype2
LIBS    = -lX11 -lfontconfig -lXft -lXrender
CFLAGS  = -std=c99 -pedantic -Wall -Werror -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_POSIX_C_SOURCE=2 $(INCS)
LDFLAGS = $(LIBS)
SRC     = $(NAME).c
OBJ     = $(SRC:.c=.o)

all: $(NAME)

$(NAME): $(OBJ)
	$(CC) $^ $(LDFLAGS) -o $@

.SUFFIXES: .c .o
$(OBJ):
.c.o:
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f -- $(NAME) $(OBJ)

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f $(NAME) $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(NAME)
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	cp $(NAME).1 $(DESTDIR)$(MANPREFIX)/man1/
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/$(NAME).1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(NAME)
	rm -f $(DESTDIR)$(MANPREFIX)/man1/$(NAME).1

.PHONY: all clean install uninstall
