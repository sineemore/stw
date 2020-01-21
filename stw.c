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
static char align = 'l';
static char *text;
static size_t len;
static size_t cap;
static FILE *inputf;
static pid_t cmdpid;
static int sfd, xfd;
static char **cmd;

static void
usage()
{
	die(
"usage: %s\n\
	[-g geometry]\n\
	[-a alignment]\n\
	[-f foreground]\n\
	[-b background]\n\
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
	
	Fnt *fnt = drw_fontset_create(drw, fonts, LENGTH(fonts));
	if (!fnt)
		die("no fonts could be loaded");

	Clr *clr = drw_scm_create(drw, (const char *[]){f, b}, 2);
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
		{.fd = 0,   .events = POLLIN},
		{.fd = sfd, .events = POLLIN},
		{.fd = xfd, .events = POLLIN}
	};
	
	for (;;) {

		if (cmdpid == 0) {
			if (inputf != NULL)
				fclose(inputf);
			start_cmd();
		}

		int dirty = 0;
		fds[0].fd = fileno(inputf);
		for (int i = 0; i < LENGTH(fds); i++)
			fds[i].revents = 0;

		if (-1 == poll(fds, LENGTH(fds), -1))
			die("poll:");
		
		if (fds[0].revents & POLLIN) {
			read_text();
			draw(drw, fnt);
			dirty = 1;
		}

		if (fds[1].revents & POLLIN) {
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
				if (p == cmdpid && (WIFEXITED(wstatus) || WIFSIGNALED(wstatus)))
					cmdpid = 0;
			}
		}
		
		if (fds[2].revents & POLLIN) {
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
	}
}