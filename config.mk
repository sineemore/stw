# See LICENSE file for copyright and license details. 

# paths
PREFIX    = /usr/local
MANPREFIX = $(PREFIX)/share/man

# includes and libs
INCS = -I/usr/include/X11 `pkg-config --cflags fontconfig`
LIBS = -lX11 -lXft -lXrender `pkg-config --libs fontconfig`

# flags
CPPFLAGS = -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_POSIX_C_SOURCE=2
CFLAGS   = -std=c99 -pedantic -Wall -Werror $(INCS) $(CPPFLAGS)
LDFLAGS  = $(LIBS)

# compiler and linker
CC = cc
