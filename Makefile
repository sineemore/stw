.POSIX:
.SUFFIXES:
NAME    = stw
CC      = cc
INCS    = -I/usr/include/X11 -I/usr/include/freetype2
LIBS    = -lX11 -lfontconfig -lXft
CFLAGS  = -g -std=c99 -pedantic -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_POSIX_C_SOURCE=2 $(INCS)
LDFLAGS = $(LIBS)
SRC     = $(NAME).c drw.c util.c
OBJ     = ${SRC:.c=.o}

all: $(NAME)

$(NAME): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

.SUFFIXES: .c .o
$(OBJ):
.c.o:
	$(CC) $(CFLAGS) -c $<

install: all
	mkdir -p $(PREFIX)/bin
	cp -f $(NAME) $(PREFIX)/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/$(NAME)

clean:
	rm -f -- $(NAME) $(OBJ)

.PHONY: clean