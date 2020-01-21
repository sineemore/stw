/* See LICENSE file for copyright and license details. */
#include <errno.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/signalfd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>

#include "arg.h"
#include "drw.h"
#include "util.h"
#include "config.h"

#define LENGTH(X) (sizeof X / sizeof X[0])
#define INITIAL_CAPACITY 64

static char *argv0;
static unsigned int sw, sh;
static unsigned int mw, mh;
static char *text;
static size_t len;
static size_t cap;
static FILE *inputf;
static pid_t cmdpid;
static int sfd, xfd;
static char **cmd;
struct timespec last;

static void
usage()
{
	die(
"usage: %s\n\
	[-g geometry]\n\
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
start_cmd() {
	int fds[2];
	if (-1 == pipe(fds))
		die("pipe:");

	inputf = fdopen(fds[0], "r");
	if (inputf == NULL)
		die("fdopen:");

	switch (cmdpid = fork()) {
	case -1:
		die("fork:");
	case 0:
		close(sfd);
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
		if (NULL == fgets(line, cap - len, inputf)) {
			if (feof(inputf)) {
				break;
			} else {
				die("error reading stdin");
			}
		}

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
draw(Drw *drw, Fnt *fnt)
{
	unsigned int prev_mw = mw;
	unsigned int prev_mh = mh;
	mw = 0;
	mh = 0;

	char *line = text;
	while (line < text + len) {
		int linelen = strlen(line);
		unsigned int w, h;
		drw_font_getexts(fnt, line, linelen,
			&w, &h);
		if (w > mw)
			mw = w;
		mh += h;
		line += linelen + 1;
	}

	if (prev_mw != mw || prev_mh != mh)
		drw_resize(drw, mw + borderpx * 2, mh + borderpx * 2);
	
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
		
		drw_text(drw, ax + borderpx, y + borderpx, w, h, 0, line, 0);
		y += h;
		line += linelen + 1;
	}
}

int
main(int argc, char *argv[])
{
	int left = 0;
	int top = 0;
	char *font = NULL;

	ARGBEGIN {
	case 'g':
		{
			unsigned int t;
			XParseGeometry(EARGF(usage()),
				&left, &top, &t, &t);
		}
		break;
	case 'f':
		colors[0] = EARGF(usage());
		break;
	case 'b':
		colors[1] = EARGF(usage());
		break;
	case 'F':
		font = EARGF(usage());
		break;
	case 'B':
		if (stoi(EARGF(usage()), &borderpx))
			usage();
		break;
	case 'a':
		{
			const char *a = EARGF(usage());
			align = a[0];
			if (strlen(a) != 1 \
			|| align != 'l' && align != 'r' && align != 'c')
				usage();
		}
		break;
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

	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGCHLD);

	if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1)
		die("sigprocmask:");
	
	sfd = signalfd(-1, &mask, 0);
	if (sfd == -1)
		die("signalfd:");

	Display *dpy = XOpenDisplay(NULL);
	if (!dpy)
		die("cannot open display");
	
	int screen = DefaultScreen(dpy);
	Window root = RootWindow(dpy, screen);

	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);

	Drw *drw = drw_create(dpy, screen, root, 300, 300);

	Fnt *fnt = drw_fontset_create(drw,
		font ? (const char *[]){font} : fonts,
		font ? 1 : LENGTH(fonts));
	if (!fnt)
		die("no fonts could be loaded");

	Clr *clr = drw_scm_create(drw, colors, 2);
	drw_setscheme(drw, clr);
	
	XSetWindowAttributes swa;
	swa.override_redirect = True;
	swa.background_pixel = clr[1].pixel;
	swa.event_mask = ExposureMask | ButtonPressMask;
	
	Window win = XCreateWindow(dpy, root,
		-1, -1, 1, 1,
		0,
		CopyFromParent, CopyFromParent, CopyFromParent,
		CWOverrideRedirect | CWBackPixel | CWEventMask,
		&swa);
	
	XLowerWindow(dpy, win);
	XMapWindow(dpy, win);

	XSelectInput(dpy, win, swa.event_mask);	
	XSync(dpy, True);

	xfd = ConnectionNumber(dpy);

	struct pollfd fds[] = {
		{.fd = sfd, .events = POLLIN},
		{.fd = xfd, .events = POLLIN},
		{.fd = 0,   .events = POLLIN}
	};
	
	for (;;) {

		struct timespec now;
		if (clock_gettime(CLOCK_BOOTTIME, &now) == -1)
			die("clock_gettime:");

		int timeout = cmdpid == 0
			? last.tv_sec + period - now.tv_sec
			: -1;

		if (cmdpid == 0 && timeout <= 0)
			start_cmd();

		int dirty = 0;
		fds[2].fd = fileno(inputf);
		for (int i = 0; i < LENGTH(fds); i++)
			fds[i].revents = 0;

		if (-1 == poll(fds, LENGTH(fds) - (cmdpid == 0), cmdpid == 0 ? timeout : -1))
			die("poll:");

		if (fds[0].revents & POLLIN) {
			struct signalfd_siginfo tmp;
			if (-1 == read(sfd, &tmp, sizeof(tmp)))
				die("read signalfd:");
			for (;;) {
				int wstatus;
				pid_t p = waitpid(-1, &wstatus, WNOHANG);
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
					if (inputf != NULL)
						fclose(inputf);
					if (clock_gettime(CLOCK_BOOTTIME, &last) == -1)
						die("clock_gettime:");
				}
			}
		}
		
		if (fds[1].revents & POLLIN) {
			XEvent ev;
			if (XNextEvent(dpy, &ev))
				break;
			if (ev.type == Expose) {
				XExposeEvent *ee = &ev.xexpose;
        			if (ee->count == 0)
					dirty = 1;
			} else if (ev.type == ButtonPress) {
				if (cmdpid)
					kill(cmdpid, SIGTERM);
				last = (struct timespec){0};
			}
		}

		if (fds[2].revents & POLLIN) {
			read_text();
			draw(drw, fnt);
			dirty = 1;
		}

		if (dirty && mw > 0 && mh > 0) {
			int x, y;

			if (signbit((float)left)) {
				x = sw + left - mw - borderpx * 2;
			} else {
				x = left;
			}

			if (signbit((float)top)) {
				y = sh + top - mh - borderpx * 2;
			} else {
				y = top;
			}

			XMoveResizeWindow(dpy, win,
				x, y,
				mw + borderpx * 2, mh + borderpx * 2);
			XSync(dpy, True);
			drw_map(drw, win, 0, 0,
				mw + borderpx * 2, mh + borderpx * 2);
		}
	}
}