/* See LICENSE file for copyright and license details. */

static int borderpx = 0;
static char font[] = "monospace:size=10";

/* background opacity */
static double alpha = 1.0;

/* X window geometry */
struct g px = {0};
struct g py = {0};
struct g tx = {0};
struct g ty = {0};

/* text alignment: l, r and c for left, right and centered respectively */
static char align = 'l';

/* foreground and background colors */
static char *colors[2] = { "#000000", "#dddddd" };

/* time in seconds between subcommand runs.
0 will completely disable subcommand restarts and -1 will make them instant.
in any case a click on a window will still immediately restart subcommand */
static int period = 5;

/* delimeter string, encountered as a separate line in subcommand output
signals stw to render buffered text and continue with next frame;
it is the only valid use of non-printable characters in subcommand output */
static char delimeter[] = "\4";

static bool window_on_top = 0;