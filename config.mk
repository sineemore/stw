# See LICENSE file for copyright and license details. 

# paths
PREFIX    = /usr/local
MANPREFIX = $(PREFIX)/share/man

# includes and libs
INCS = -I/usr/include/X11 -I/usr/include/freetype2
LIBS = -lX11 -lfontconfig -lXft -lXrender

# flags
CPPFLAGS = -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_POSIX_C_SOURCE=2
CFLAGS   = -std=c99 -pedantic -Wall -Werror $(INCS) $(CPPFLAGS)
LDFLAGS  = $(LIBS)

# compiler and linker
CC = cc
