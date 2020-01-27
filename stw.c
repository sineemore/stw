/* See LICENSE file for copyright and license details. */
#include <errno.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>

#include "arg.h"
#include "drw.h"
#include "util.h"

struct g {
	int value;
	char prefix;
	char suffix;
};

#include "config.h"

#define LENGTH(X) (sizeof X / sizeof X[0])
#define INITIAL_CAPACITY 2

static char *argv0;
static Display *dpy;
static int xfd;
static Drw *drw;
static Fnt *fnt;
static Window win;
static unsigned int sw, sh;
static unsigned int mw, mh;
static char **cmd;
static pid_t cmdpid;
static FILE *inputf;
static char *text;
static size_t len;
static size_t cap;
static int spipe[2];

static void
usage()
{
	die(
"usage: %s\n\
	[-x x position]\n\
	[-y y position]\n\
	[-X x translate]\n\
	[-Y y translate]\n\
	[-a alignment]\n\
	[-f foreground]\n\
	[-b background]\n\
	[-F font]\n\
	[-B borderpx]\n\
	[-p period]\n\
	command [args ...]",
argv0);
}

static void
signal_handler(int s)
{
	if (-1 == write(spipe[1], s == SIGCHLD ? "c" : "a", 1))
		abort();
}

static void
start_cmd()
{
	int fds[2];
	if (-1 == pipe(fds))
		die("pipe:");

	inputf = fdopen(fds[0], "r");
	if (inputf == NULL)
		die("fdopen:");

	cmdpid = fork();
	switch (cmdpid) {
	case -1:
		die("fork:");
	case 0:
		close(spipe[0]);
		close(spipe[1]);
		close(xfd);
		close(fds[0]);
		dup2(fds[1], STDOUT_FILENO);
		execvp(cmd[0], cmd);
		exit(1);
	}

	close(fds[1]);
}

static void
read_text()
{
	int dlen = strlen(delimeter);

	len = 0;
	for (;;) {
		if (len + dlen + 2 > cap) {
			// buffer must have sufficient capacity to
			// store delimeter string, \n and \0 in one read
			cap = cap ? cap * 2 : INITIAL_CAPACITY;
			text = realloc(text, cap);
			if (text == NULL)
				die("realloc:");
		}

		char *line = &text[len];
		if (NULL == fgets(line, cap - len, inputf)) {
			if (feof(inputf)) {
				break;
			} else {
				die("error reading stdin");
			}
		}

		int llen = strlen(line);

		if (line[llen - 1] == '\n') {
			line[--llen] = '\0';
			len += llen + 1;
		} else {
			len += llen;
		}

		if (llen == dlen && strcmp(line, delimeter) == 0) {
			len -= dlen + 2;
			break;
		}
	}
}

static void
draw()
{
	unsigned int prev_mw = mw;
	unsigned int prev_mh = mh;
	mw = 0;
	mh = 0;

	char *line = text;
	while (line < text + len) {
		int llen = strlen(line);
		unsigned int w, h;
		drw_font_getexts(fnt, line, llen, &w, &h);
		if (w > mw)
			mw = w;

		mh += h;
		line += llen + 1;
	}

	if (prev_mw != mw || prev_mh != mh)
		drw_resize(drw, mw + borderpx * 2, mh + borderpx * 2);

	drw_rect(drw, 0, 0, sw, sh, 1, 1);

	unsigned int x = 0;
	unsigned int y = 0;

	line = text;
	while (line < text + len) {
		int llen = strlen(line);
		unsigned int w, h;
		drw_font_getexts(fnt, line, llen, &w, &h);

		// text alignment
		int ax = x;
		if (align == 'r') {
			ax = mw - w;
		} else if (align == 'c') {
			ax = (mw - w) / 2;
		}

		drw_text(drw, ax + borderpx, y + borderpx, w, h, 0, line, 0);
		y += h;
		line += llen + 1;
	}
}

static void
reap()
{
	for (;;) {
		int wstatus;
		pid_t p = waitpid(-1, &wstatus, cmdpid == 0 ? WNOHANG : 0);
		if (p == -1) {
			if (cmdpid == 0 && errno == ECHILD) {
				errno = 0;
				break;
			}
			die("waitpid:");
		}
		if (p == 0)
			break;
		if (p == cmdpid && (WIFEXITED(wstatus) || WIFSIGNALED(wstatus))) {
			cmdpid = 0;
		}
	}
	if (fclose(inputf) == -1)
		die("close:");
}

static void
run()
{
	struct pollfd fds[] = {
		{.fd = spipe[0], .events = POLLIN}, // signals
		{.fd = xfd,      .events = POLLIN}, // xlib
		{.fd = 0,        .events = POLLIN}  // cmd stdout (set later)
	};

	int restart_now = 1;
	for (;;) {
		if (restart_now && cmdpid == 0) {
			restart_now = 0;
			start_cmd();
		}

		int dirty = 0;
		fds[2].fd = cmdpid != 0 ? fileno(inputf) : 0;
		for (int i = 0; i < LENGTH(fds); i++)
			fds[i].revents = 0;

		if (-1 == poll(fds, LENGTH(fds) - (cmdpid == 0), -1)) {
			if (errno == EINTR) {
				errno = 0;
				continue;
			}
			die("poll:");
		}


		if (fds[0].revents & POLLIN) {
			// signals
			char s;
			if (-1 == read(spipe[0], &s, 1))
				die("sigpipe read:");

			if (s == 'c') {
				// sigchld received
				reap();
				if (!restart_now) {
					alarm(period);
				}
			} else if (s == 'a' && cmdpid == 0) {
				// sigalrm received
				start_cmd();
			}
		}

		if (fds[1].revents & POLLIN) {
			// xlib
			XEvent ev;
			if (XNextEvent(dpy, &ev))
				break;

			if (ev.type == Expose) {
				if (ev.xexpose.count == 0) {
					dirty = 1;
				}
			} else if (ev.type == ButtonPress) {
				if (cmdpid && kill(cmdpid, SIGTERM) == -1)
					die("kill:");
				alarm(0);
				restart_now = 1;
			}
		}

		if (fds[2].revents & POLLIN) {
			// cmd stdout
			read_text();
			draw();
			dirty = 1;
		}

		if (dirty && mw > 0 && mh > 0) {
			// window redraw
			int x = px.suffix == '%'
				? (px.value / 100.0) * sw
				: px.value;

			if (px.prefix == '-') {
				x = sw - x - mw - borderpx * 2;
			}

			if (tx.value != '0') {
				int v = tx.value;
				if (tx.suffix == '%')
					v = (v / 100.0) * mw;
				if (tx.prefix == '-')
					v *= -1;
				x += v;
			}

			int y = py.suffix == '%'
				? (py.value / 100.0) * sh
				: py.value;

			if (py.prefix == '-') {
				y = sh - y - mh - borderpx * 2;
			}

			if (ty.value != '0') {
				int v = ty.value;
				if (ty.suffix == '%')
					v = (v / 100.0) * mh;
				if (ty.prefix == '-')
					v *= -1;
				y += v;
			}

			XMoveResizeWindow(
				dpy, win, x, y,
				mw + borderpx * 2, mh + borderpx * 2
			);
			XSync(dpy, True);
			drw_map(
				drw, win, 0, 0,
				mw + borderpx * 2, mh + borderpx * 2
			);
		}
	}
}

static void
setup(char *font)
{
	// self pipe and signal handler

	if (pipe(spipe) == -1)
		die("pipe:");

	struct sigaction sa = {0};
	sa.sa_handler = signal_handler;
	sa.sa_flags = SA_RESTART;

	if (sigaction(SIGCHLD, &sa, NULL) == -1
	|| sigaction(SIGALRM, &sa, NULL) == -1)
		die("sigaction:");

	// xlib

	dpy = XOpenDisplay(NULL);
	if (!dpy)
		die("cannot open display");

	xfd = ConnectionNumber(dpy);

	int screen = DefaultScreen(dpy);
	Window root = RootWindow(dpy, screen);

	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);


	// drw drawing context

	drw = drw_create(dpy, screen, root, 300, 300);

	fnt = drw_fontset_create(drw, &font, 1);
	if (!fnt)
		die("no fonts could be loaded");

	Clr *clr = drw_scm_create(drw, colors, 2);
	drw_setscheme(drw, clr);


	// x window

	XSetWindowAttributes swa;
	swa.override_redirect = True;
	swa.background_pixel = clr[1].pixel;
	swa.event_mask = ExposureMask | ButtonPressMask;

	win = XCreateWindow(
		dpy, root,
		-1, -1, 1, 1, 0,
		CopyFromParent, CopyFromParent, CopyFromParent,
		CWOverrideRedirect | CWBackPixel | CWEventMask,
		&swa
	);

	XLowerWindow(dpy, win);
	XMapWindow(dpy, win);
	XSelectInput(dpy, win, swa.event_mask);
	XSync(dpy, True);
}

static int
parsegeom(char *b, char *prefix, char *suffix, struct g *g)
{
	g->prefix = 0;
	g->suffix = 0;

	if (b[0] < '0' || b[0] > '9') {
		for (char *t = prefix; *t; t++) {
			if (*t == b[0]) {
				g->prefix = b[0];
				break;
			}
		}
		if (g->prefix == 0) {
			return -1;
		}
		b++;
	}

	if (b[0] < '0' || b[0] > '9')
		return -1;

	char *e;
	long int li = strtol(b, &e, 10);

	if (b == e || li < INT_MIN || li > INT_MAX)
		return -1;

	g->value = (int)li;
	b = e;

	if (b[0] != '\0') {
		for (char *t = suffix; *t; t++) {
			if (*t == b[0]) {
				g->suffix = b[0];
				break;
			}
		}
		if (g->suffix == 0 || b[1] != '\0') {
			return -1;
		}
	}

	return 0;
}

int
main(int argc, char *argv[])
{
	char *xfont = font;

	ARGBEGIN {
	case 'x':
		if (-1 == parsegeom(EARGF(usage()), "+-", "%", &px))
			usage();
		break;
	case 'y':
		if (-1 == parsegeom(EARGF(usage()), "+-", "%", &py))
			usage();
		break;
	case 'X':
		if (-1 == parsegeom(EARGF(usage()), "+-", "%", &tx))
			usage();
		break;
	case 'Y':
		if (-1 == parsegeom(EARGF(usage()), "+-", "%", &ty))
			usage();
		break;
	case 'f':
		colors[0] = EARGF(usage());
		break;
	case 'b':
		colors[1] = EARGF(usage());
		break;
	case 'F':
		xfont = EARGF(usage());
		break;
	case 'B':
		if (stoi(EARGF(usage()), &borderpx))
			usage();
		break;
	case 'a': {
		const char *a = EARGF(usage());
		align = a[0];
		if (strlen(a) != 1 \
		|| align != 'l' && align != 'r' && align != 'c')
			usage();
	} break;
	case 'p':
		if (stoi(EARGF(usage()), &period))
			usage();
		break;
	default:
		usage();
	} ARGEND

	if (argc == 0)
		usage();

	cmd = argv;

	setup(xfont);
	run();
}