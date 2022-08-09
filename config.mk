# stw version
VERSION := 0.4

# Customize below to fit your system

# paths
PREFIX ?= /usr/local
MANPREFIX = $(PREFIX)/share/man

# includes and libs
INCS = `pkg-config --cflags fontconfig`
LIBS = `pkg-config --libs fontconfig xft xrender`

# flags
STWCPPFLAGS = -DVERSION=\"$(VERSION)\" -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_POSIX_C_SOURCE=2 $(INCS)
STWCFLAGS   = -std=c99 -pedantic -Wall -Wno-shadow -Wno-sign-compare $(STWCPPFLAGS) $(CPPFLAGS) $(CFLAGS)
STWLDFLAGS  = $(LIBS) $(LDFLAGS)
