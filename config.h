/* See LICENSE file for copyright and license details. */

static int borderpx = 0;
static char font[] = "monospace:size=10";

struct g px = {.prefix = '-'};
struct g py = {.prefix = '-'};
struct g tx = {0};
struct g ty = {0};

/* text alignment: l, r and c for left, right and centered respectively */
static char align = 'l';

/* foreground and background colors */
static char *colors[2] = { "#000000", "#dddddd" };

/* time in seconds between command runs */
static int period = 5;

/* delimeter string, encountered as a separate line in subcommand output
signals stw to render buffered text and continue with next frame;
it is the only valid use of non-printable characters in subcommand output */
static char delimeter[] = "\4";