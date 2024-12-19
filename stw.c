#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <regex.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "arg.h"

#include "config.h"

#define LENGTH(x) (sizeof(x) / sizeof(*x))
#define INITIAL_CAPACITY 64

typedef struct {
        size_t offset;
        size_t len;
        XGlyphInfo ex;
        bool clickable;
        bool eol;
        short x, y, w, h;
} stw_word;

typedef struct _window {
        int w;
        int h;
        bool hidden;
} stw_window;

typedef struct {
        XftColor fg;
        XftColor bg;
} stw_xcolorscheme;

stw_word *words;
size_t words_len, words_cap;
char *buf;
size_t buf_len, buf_cap;
regex_t clickable_re;
regmatch_t pmatch[1];
char *argv0, **cmd;
int spipe[2];
char regerror_buf[100];
Display *dpy;
int xfd;
int depth = 32;
Window root, win;
Drawable drawable;
GC gc;
XftDraw *xdraw;
int scheme = 0;
stw_xcolorscheme *xschemes;
XftFont *xfont;
unsigned int screen_width, screen_height;
bool overlay = false;

void die(const char *fmt, ...)
{
        int tmp = errno;
        va_list ap;

        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);

        if (fmt[0] && fmt[strlen(fmt) - 1] == ':') {
                fputc(' ', stderr);
                errno = tmp;
                perror(NULL);
        } else {
                fputc('\n', stderr);
        }

        exit(1);
}

void usage()
{
        die("\
usage: stw [-t ] [-g geometry] [-c colors] [-f font] [-a align] [-b borderpx]\n\
           [-p period] [-A bgalpha] [-r regexp] command [arg ...]");
}

void signalhandler(int s)
{
        if (-1 == write(spipe[1], "a", 1))
                abort();
}

char *stw_regerror(int err)
{
        regerror(err, &clickable_re, regerror_buf, sizeof(regerror_buf));
        return regerror_buf;
}

void runcmd(char *selected)
{
        int fds[2];
        if (-1 == pipe(fds))
                die("pipe:");

        FILE *f = fdopen(fds[0], "r");
        if (f == NULL)
                die("fdopen:");

        int cmdpid = fork();
        if (cmdpid == -1)
                die("fork:");

        if (cmdpid == 0) {
                if (selected)
                        setenv("STW_SELECTED", selected, 1);

                close(spipe[0]);
                close(spipe[1]);
                close(xfd);
                close(fds[0]);
                dup2(fds[1], STDOUT_FILENO);
                close(fds[1]);
                execvp(cmd[0], cmd);
                exit(1);
        }

        close(fds[1]);

        buf_len = 0;
        while (f != NULL) {
                if (buf_len + 1 >= buf_cap) {
                        buf_cap = buf_cap ? buf_cap * 2 : INITIAL_CAPACITY;
                        buf = realloc(buf, buf_cap);
                        if (buf == NULL)
                                die("realloc:");
                }

                char *s = buf + buf_len;
                if (NULL == fgets(s, buf_cap - buf_len, f)) {
                        if (feof(f)) {
                                if (fclose(f) == -1)
                                        die("fclose subcommand output:");
                                f = NULL;
                        } else {
                                die("fgets subcommand output:");
                        }
                }

                int l = strlen(s);
                if (s[l - 1] == '\n') {
                        s[l - 1] = '\0';
                }

                buf_len += l;
        }

        words_len = 0;
        size_t offset = 0;
        while (offset < buf_len) {
                if (words_len + 1 >= words_cap) {
                        words_cap = words_cap ? words_cap * 2 : INITIAL_CAPACITY;
                        words = realloc(words, words_cap * sizeof(stw_word));
                        if (words == NULL)
                                die("realloc:");
                }

                size_t len;
                bool click = false;

                if (clickable[0] == '\0' || regexec(&clickable_re, buf + offset, 1, pmatch, 0) == REG_NOMATCH) {
                        len = strlen(buf + offset);
                } else {
                        if (pmatch[0].rm_so > 0) {
                                len = pmatch[0].rm_so;
                        } else {
                                len = pmatch[0].rm_eo;
                                click = true;
                        }
                }

                bool eol = offset + len >= buf_len || buf[offset + len] == '\0';

                words[words_len++] = (stw_word){
                    .offset = offset,
                    .len = len,
                    .clickable = click,
                    .eol = eol,
                };

                offset += len + (eol ? 1 : 0);
        }

        for (;;) {
                int s;
                if (-1 == waitpid(cmdpid, &s, 0))
                        die("waitpid:");

                if (WIFEXITED(s) || WIFSIGNALED(s))
                        break;
        }
}

int pos(stw_geometry g, int size)
{
        int sign = g.prefix == '-' ? -1 : 1;
        return g.suffix == '%' ? sign * (g.value / 100.0) * size : sign * g.value;
}

stw_word *findword(int x, int y)
{
        for (int i = 0; i < words_len; i++) {
                stw_word *w = &words[i];
                if (w->clickable) {
                        if (x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h) {
                                return w;
                        }
                }
        }

        return NULL;
}

stw_window calc(stw_window prev)
{
        int width = 0;
        int height = 0;

        int line_width = 0;
        for (size_t i = 0; i < words_len; i++) {
                stw_word *w = &words[i];
                XftTextExtentsUtf8(dpy, xfont, (unsigned char *)buf + w->offset, w->len, &w->ex);
                line_width += w->ex.xOff;
                if (w->eol) {
                        if (line_width > width)
                                width = line_width;
                        height += xfont->ascent + xfont->descent;
                        line_width = 0;
                }
        }

        bool hidden = width == 0 || height == 0;

        int x = -1;
        int y = borderpx;
        for (size_t i = 0; i < words_len; i++) {
                stw_word *w = &words[i];

                if (x == -1) {
                        if (align == 'l') {
                                x = borderpx;
                        } else {
                                int tw = 0;
                                for (int j = i; j < words_len; j++) {
                                        tw += words[j].ex.xOff;
                                        if (words[j].eol)
                                                break;
                                }

                                if (align == 'r')
                                        x = borderpx + width - tw;
                                else
                                        x = borderpx + (width - tw) / 2;
                        }
                }

                w->x = x;
                w->y = y;
                w->w = w->ex.xOff;
                w->h = xfont->ascent + xfont->descent;

                if (w->eol) {
                        x = -1;
                        y += w->h;
                } else {
                        x += w->w;
                }
        }

        width += borderpx * 2;
        height += borderpx * 2;

        // TODO: Move this to a separate function maybe and keep calc pure
        if (width != prev.w || height != prev.h || !hidden) {
                XFreePixmap(dpy, drawable);
                drawable = XCreatePixmap(dpy, root, width, height, depth);
                if (!drawable)
                        die("cannot allocate drawable");
                XftDrawChange(xdraw, drawable);
        }

        return (stw_window){
            width,
            height,
            hidden,
        };
}

void draw(stw_window win, stw_word *selected)
{
        if (win.hidden)
                return;

        stw_xcolorscheme cs = xschemes[scheme];
        XSetForeground(dpy, gc, cs.bg.pixel);
        XFillRectangle(dpy, drawable, gc, 0, 0, win.w, win.h);

        for (size_t i = 0; i < words_len; i++) {
                stw_word *w = &words[i];
                bool hovered = selected == w;
                cs = xschemes[scheme + (hovered ? 1 : 0)];
                if (hovered) {
                        XSetForeground(dpy, gc, cs.bg.pixel);
                        XFillRectangle(dpy, drawable, gc, w->x, w->y, w->w, w->h);
                }

                XftDrawStringUtf8(xdraw, &cs.fg, xfont, w->x, w->y + xfont->ascent, (unsigned char *)buf + w->offset,
                                  w->len);
        }
}

void run()
{
        char run_now = 'a';
        bool dirty = false;
        stw_word *clicked = NULL, *selected = NULL;
        stw_window pwin = {0, 0, true};
        int pointerx = -1, pointery = -1;
        char *text = NULL;
        size_t text_cap = 0;

        for (;;) {
                stw_window nwin = pwin;

                if (run_now || period == 0) {
                        if (clicked) {
                                if (text_cap < clicked->len + 1) {
                                        text_cap = clicked->len + 1;
                                        text = realloc(text, text_cap);
                                        if (text == NULL)
                                                die("realloc:");
                                }

                                memcpy(text, buf + clicked->offset, clicked->len);
                                text[clicked->len] = '\0';
                        }

                        runcmd(run_now == 'c' ? (clicked ? text : "") : NULL);
                        run_now = 0;
                        clicked = NULL;
                        nwin = calc(pwin);
                        if (pointerx >= 0 && pointery >= 0)
                                selected = findword(pointerx, pointery);
                        draw(nwin, selected);
                        dirty = true;
                        if (period > 0)
                                alarm(period);
                }

                if (pwin.hidden != nwin.hidden) {
                        if (nwin.hidden) {
                                XUnmapWindow(dpy, win);
                                XSync(dpy, False);
                        } else {
                                XMapWindow(dpy, win);
                        }
                }

                if (dirty) {
                        if (window_on_top)
                                XRaiseWindow(dpy, win);
                        else
                                XLowerWindow(dpy, win);

                        if (nwin.h != pwin.h || nwin.w != pwin.w) {
                                int wx = pos(px, screen_width);
                                if (px.prefix == '-')
                                        wx = screen_width + wx - nwin.w;

                                wx += pos(tx, nwin.w);

                                int wy = pos(py, screen_height);
                                if (py.prefix == '-')
                                        wy = screen_height + wy - nwin.h;

                                wy += pos(ty, nwin.h);

                                XMoveResizeWindow(dpy, win, wx, wy, nwin.w, nwin.h);
                        }

                        XCopyArea(dpy, drawable, win, gc, 0, 0, nwin.w, nwin.h, 0, 0);
                        XSync(dpy, False);

                        dirty = false;
                        pwin = nwin;
                }

                struct pollfd fds[] = {
                    {.fd = spipe[0], .events = POLLIN},
                    {.fd = xfd, .events = POLLIN},
                };

                if (-1 == poll(fds, LENGTH(fds), period == 0 ? 0 : -1)) {
                        if (errno == EINTR) {
                                errno = 0;
                                continue;
                        }
                        die("poll:");
                }

                if (fds[0].revents & POLLIN) {
                        char s;
                        if (-1 == read(spipe[0], &s, 1))
                                die("sigpipe read:");

                        if (s == 'a')
                                run_now = 'a';
                }

                if (fds[1].revents & POLLIN) {
                        while (XPending(dpy)) {
                                XEvent ev;
                                XNextEvent(dpy, &ev);

                                if (ev.type == Expose) {
                                        dirty = dirty || ev.xexpose.count == 0;
                                } else if (ev.type == ButtonPress) {
                                        alarm(0);
                                        clicked = findword(ev.xbutton.x, ev.xbutton.y);
                                        run_now = 'c';
                                } else if (ev.type == MotionNotify) {
                                        pointerx = ev.xmotion.x;
                                        pointery = ev.xmotion.y;
                                        stw_word *t = findword(pointerx, pointery);
                                        if (t != selected) {
                                                selected = t;
                                                draw(nwin, selected);
                                                dirty = true;
                                        }
                                } else if (ev.type == LeaveNotify) {
                                        pointerx = -1;
                                        pointery = -1;
                                        if (selected) {
                                                selected = NULL;
                                                draw(nwin, selected);
                                                dirty = true;
                                        }
                                }
                        }
                }
        }
}

void blend(XftColor *c, double alpha)
{
        c->pixel &= 0x00FFFFFF;
        unsigned char r = ((c->pixel >> 16) & 0xff) * alpha;
        unsigned char g = ((c->pixel >> 8) & 0xff) * alpha;
        unsigned char b = (c->pixel & 0xff) * alpha;
        c->pixel = (r << 16) + (g << 8) + b;
        c->pixel |= (unsigned char)(0xff * alpha) << 24;
}

void setup()
{
        if (pipe(spipe) == -1)
                die("pipe:");

        struct sigaction sa = {0};
        sa.sa_handler = signalhandler;
        sa.sa_flags = SA_RESTART;

        if (-1 == sigaction(SIGALRM, &sa, NULL))
                die("sigaction:");

        dpy = XOpenDisplay(NULL);
        if (!dpy)
                die("cannot open display");

        xfd = ConnectionNumber(dpy);
        int xscreen = DefaultScreen(dpy);
        root = RootWindow(dpy, xscreen);
        screen_width = DisplayWidth(dpy, xscreen);
        screen_height = DisplayHeight(dpy, xscreen);

        XVisualInfo xvi = {.screen = xscreen, .depth = depth, .class = TrueColor};
        XMatchVisualInfo(dpy, xscreen, xvi.depth, TrueColor, &xvi);
        Visual *xvisual = xvi.visual;

        Colormap xcolormap = XCreateColormap(dpy, root, xvisual, None);
        drawable = XCreatePixmap(dpy, root, 1, 1, xvi.depth);
        xdraw = XftDrawCreate(dpy, drawable, xvisual, xcolormap);
        xfont = XftFontOpenName(dpy, xscreen, font);
        if (!xfont)
                die("cannot load font");

        xschemes = calloc(LENGTH(schemes), sizeof(stw_xcolorscheme));
        if (xschemes == NULL)
                die("calloc:");

        for (int i = 0; i < LENGTH(schemes); i++) {
                if (!XftColorAllocName(dpy, xvisual, xcolormap, schemes[i].fg, &xschemes[i].fg) ||
                    !XftColorAllocName(dpy, xvisual, xcolormap, schemes[i].bg, &xschemes[i].bg))
                        die("cannot allocate colors");
                if (i == 0)
                        blend(&xschemes[i].bg, bgalpha);
        }

        XSetWindowAttributes swa;
        swa.override_redirect = True;
        swa.colormap = xcolormap;
        swa.event_mask = ExposureMask | ButtonPressMask | PointerMotionMask | LeaveWindowMask | VisibilityChangeMask;
        win = XCreateWindow(dpy, root, -1, -1, 1, 1, 0, xvi.depth, InputOutput, xvisual,
                            CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWEventMask | CWColormap, &swa);

        // Fixed region to allow passthrough
        if (overlay) {
                XRectangle rect;
                XserverRegion region = XFixesCreateRegion(dpy, &rect, 1);
                XFixesSetWindowShapeRegion(dpy, win, ShapeInput, 0, 0, region);
                XFixesDestroyRegion(dpy, region);
        }

        XGCValues gcvalues = {0};
        gcvalues.graphics_exposures = False;
        gc = XCreateGC(dpy, drawable, GCGraphicsExposures, &gcvalues);

        XSelectInput(dpy, win, swa.event_mask);
}

int parsecomma(char *b, int (*parse)(int, char *, size_t))
{
        for (int i = 0;; i++) {
                char *c = strchr(b, ',');
                int l = c ? c - b : strlen(b);

                if (parse(i, b, l))
                        return -1;

                b += l + (c ? 1 : 0);

                if (!*b && !c)
                        break;
        }

        return 0;
}

int parsecolorscheme(int i, char *b, size_t l)
{
        if (i >= LENGTH(schemes) * 2)
                die("too many colors");

        if (l == 0)
                return 0;

        char *s = malloc(l + 1);
        if (!s)
                die("malloc:");

        strncpy(s, b, l);

        if (!(i % 2))
                schemes[i / 2].fg = s;
        else
                schemes[i / 2].bg = s;

        return 0;
}

int parsegeometry(int i, char *b, size_t l)
{
        stw_geometry *g = (stw_geometry *[]){&px, &py, &tx, &ty, NULL}[i];
        if (g == NULL)
                die("to many geometry options");

        if (l == 0)
                return 0;

        char *prefix = "+-";
        char *suffix = "%";
        char *o = b;

        g->prefix = *prefix;
        g->suffix = 0;
        for (char *t = prefix; *t; t++) {
                if (*t == b[0]) {
                        g->prefix = *b;
                        b++;
                        break;
                }
        }

        if (*b < '0' || *b > '9')
                return -1;

        char *e;
        g->value = strtol(b, &e, 10);
        if (b == e)
                return -1;

        if (e - o < l) {
                for (char *t = suffix; *t; t++) {
                        if (*t == *e) {
                                g->suffix = *e;
                                e++;
                                break;
                        }
                }
        }

        if (e - o < l)
                return -1;

        return 0;
}

int stoi(char *s, int *r)
{
        char *e;
        long int li = strtol(s, &e, 10);
        *r = (int)li;
        return ((s[0] < '0' || s[0] > '9') && s[0] != '-') || li < INT_MIN || li > INT_MAX || *e != '\0';
}

int main(int argc, char *argv[])
{
        ARGBEGIN
        {
        case 't':
                window_on_top = true;
                break;
        case 'o':
                overlay = true;
                break;
        case 'g':
                if (-1 == parsecomma(EARGF(usage()), parsegeometry))
                        usage();
                break;
        case 'c':
                if (-1 == parsecomma(EARGF(usage()), parsecolorscheme))
                        usage();
                break;
        case 'f':
                font = EARGF(usage());
                break;
        case 'a': {
                char *a = EARGF(usage());
                align = a[0];
                if (strlen(a) != 1 || (align != 'l' && align != 'r' && align != 'c'))
                        usage();
        } break;
        case 'b':
                if (stoi(EARGF(usage()), &borderpx))
                        usage();
                break;
        case 'p':
                if (stoi(EARGF(usage()), &period))
                        usage();
                break;
        case 'A': {
                char *s = EARGF(usage());
                char *end;
                bgalpha = strtod(s, &end);
                if (*s == '\0' || *end != '\0' || bgalpha < 0 || bgalpha > 1)
                        usage();
        } break;
        case 'r':
                clickable = EARGF(usage());
                break;
        default:
                usage();
        }
        ARGEND

        if (argc == 0)
                usage();

        cmd = argv;

        if (*clickable) {
                int ret = regcomp(&clickable_re, clickable, REG_EXTENDED);
                if (ret)
                        die("regcomp: %s", stw_regerror(ret));
        }

        setup();
        run();
}
