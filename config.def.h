#include <stdbool.h>

int borderpx = 0;
char *font = "monospace:size=10";
bool window_on_top = 0;

// bgalpha is the alpha value of the background color.
double bgalpha = 1.0;

typedef struct {
	char *fg;
	char *bg;
} stw_colorscheme;

stw_colorscheme schemes[] = {
	{ "black", "#cccccc" }, // regular
	{ "white", "black" }, // matched word
};

/* period is time in seconds between subcommand runs.
0 disables subcommand restarts and -1 makes them instant.
clicks on a window restarts subcommand */
int period = 5;

/* align is the text alignment: l, r, c for left, right, centered. */
char align = 'l';

/* clickable is the regex to match clickable words.
If matched text is clicked, the subcommand is restarted with
env var STW_SELECTED set to the clicked word. */
char *clickable = "";

typedef struct {
	int value;
	char prefix;
	char suffix;
} stw_geometry;

/* Default X window position */
stw_geometry px = {0};
stw_geometry py = {0};
stw_geometry tx = {0};
stw_geometry ty = {0};