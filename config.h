/* See LICENSE file for copyright and license details. */

static int borderpx = 0;
static const char *fonts[] = { "monospace:size=10" };

/* text alignment: l, r and c for left, right and centered respectively */
static char align = 'l';

/* foreground and background colors */
static char *colors[2] = { "#000000", "#dddddd" };

/* time in seconds between command runs */
static int period = 60;