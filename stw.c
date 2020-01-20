#include <math.h>
#include <errno.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>

#include "arg.h"
#include "drw.h"
#include "util.h"
#include "config.h"

#define LENGTH(X) (sizeof X / sizeof X[0])
#define INITIAL_CAPACITY 64

static char *argv0;
static Display *dpy;
static int screen;
static Window root;
static Drw *drw;
static Fnt *fnt;
static Clr *clr;
static unsigned int sw, sh;
static unsigned int mw, mh;
static char align = 'l';
static char *text;
static size_t len;
static size_t cap;

static void
usage()
{
	die("usage: %s [-g geometry]\n", argv0);
}

static void
read_text()
{
	len = 0;
	for (;;) {
		if (len + 3 >= cap) {
			// text must have sufficient space to
			// store \0\n in one read
			cap = cap ? cap * 2 : INITIAL_CAPACITY;
			text = realloc(text, cap);
			if (text == NULL)
				die("realloc:");
		}

		char *line = &text[len];
		if (NULL == fgets(line, cap - len, stdin) && !feof(stdin))
			die("error reading stdin");

		int l = strlen(line);
		if (l == 0) {
			// if strlen returns 0, than it's probably \0\n
			break;
		}

		len += l;

		if (line[l - 1] == '\n')
			line[l - 1] = '\0';
	}
}

static void
draw()
{
	mw = 0;
	mh = 0;

	char *line = text;
	while (line < text + len) {
		int linelen = strlen(line);
		unsigned int w, h;
		drw_font_getexts(fnt, line, linelen,
			&w, &h);
		if (w > mw) {
			mw = w;
		}
		mh += h;
		line += linelen + 1;
	}

	drw_resize(drw, mw, mh);
	drw_rect(drw, 0, 0, sw, sh, 1, 1);
	
	unsigned int x = 0;
	unsigned int y = 0;

	line = text;
	while (line < text + len) {
		int linelen = strlen(line);
		unsigned int w, h;
		drw_font_getexts(fnt, line, linelen,
			&w, &h);
		
		// text alignment
		int ax = x;
		if (align == 'r') {
			ax = mw - w;
		} else if (align == 'c') {
			ax = (mw - w) / 2;
		}
		
		drw_text(drw, ax, y, w, h, 0, line, 0);
		y += h;
		line += linelen + 1;
	}
}

int
main(int argc, char *argv[])
{
	int left = 0;
	int top = 0;
	unsigned int dumb;

	const char *f = fg;
	const char *b = bg;
	const char *tmp;

	ARGBEGIN {
	case 'g':
		XParseGeometry(EARGF(usage()),
                	&left, &top, &dumb, &dumb);
		break;
	case 'f':
		f = EARGF(usage());
		break;
	case 'b':
		b = EARGF(usage());
		break;
	case 'a':
		tmp = EARGF(usage());
		if (strlen(tmp) != 1)
			usage();
		align = tmp[0];
		if (align != 'l' && align != 'r' && align != 'c')
			usage();
		break;
	default:
		usage();
	} ARGEND

	dpy = XOpenDisplay(NULL);
	if (!dpy)
		die("cannot open display");
	
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);

	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);

	drw = drw_create(dpy, screen, root, 300, 300);
	
	fnt = drw_fontset_create(drw, fonts, LENGTH(fonts));
	if (!fnt)
		die("no fonts could be loaded");

	clr = drw_scm_create(drw, (const char *[]){f, b}, 2);
	drw_setscheme(drw, clr);
	
	XSetWindowAttributes swa;
	swa.override_redirect = True;
	swa.background_pixel = clr[1].pixel;
	swa.event_mask = ExposureMask | VisibilityChangeMask;
	
	Window win = XCreateWindow(dpy, root,
		-1, -1, 1, 1,
		0,
		CopyFromParent, CopyFromParent, CopyFromParent,
		CWOverrideRedirect | CWBackPixel | CWEventMask,
		&swa);
	
	XLowerWindow(dpy, win);
	XMapWindow(dpy, win);

	XSelectInput(dpy, win, swa.event_mask);	
	int xfd = ConnectionNumber(dpy);
	
	struct pollfd fds[] = {
		{.fd = STDIN_FILENO, .events = POLLIN},
		{.fd = xfd,          .events = POLLIN}
	};
	
	while (-1 != poll(fds, LENGTH(fds), -1))
	{
		int dirty = 0;
		
		if (fds[0].revents == POLLIN) {
			read_text();
			draw();
			dirty = 1;
		}
		
		if (fds[1].revents == POLLIN) {
			dirty = 1;
			XEvent ev;
			if (XNextEvent(dpy, &ev))
				break;
			if (ev.type == Expose) {
				XExposeEvent *ee = &ev.xexpose;
        			if (ee->count == 0)
					dirty = 1;
			}
		}

		if (dirty && mw > 0 && mh > 0) {
			int x, y;

			if (signbit((float)left)) {
				x = sw + left - mw;
			} else {
				x = left;
			}

			if (signbit((float)top)) {
				y = sh + top - mh;
			} else {
				y = top;
			}

			XMoveResizeWindow(dpy, win,
				x, y,
				mw, mh);
			XSync(dpy, True);
			drw_map(drw, win, 0, 0, mw, mh);
		}

		fds[0].revents = 0;
		fds[1].revents = 0;
	}
}