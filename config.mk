# Customize below to fit your system, defaults are for Void Linux

PREFIX    = /usr/local
MANPREFIX = $(PREFIX)/share/man

X11INC = /usr/include/X11
X11LIB =

PKG_CONFIG = pkg-config

INCS = -I$(X11INC) \
       `$(PKG_CONFIG) --cflags fontconfig freetype2`
LIBS = -L$(X11LIB) -lm -lrt -lX11 -lutil -lXft \
       `$(PKG_CONFIG) --libs fontconfig freetype2 xfixes`

STWCPPFLAGS = -D_XOPEN_SOURCE=600
STWCFLAGS   = $(INCS) $(STWCPPFLAGS) $(CPPFLAGS) $(CFLAGS)
STWLDFLAGS  = $(LIBS) $(LDFLAGS)

# OpenBSD: (untested)
#STWCPPFLAGS = -D_XOPEN_SOURCE=600 -D_BSD_SOURCE
#LIBS = -L$(X11LIB) -lm -lX11 -lutil -lXft \
#       `$(PKG_CONFIG) --libs fontconfig` \
#       `$(PKG_CONFIG) --libs freetype2`

# compiler and linker
# CC = c99