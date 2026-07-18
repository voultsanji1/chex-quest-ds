// NDS bottom screen panel: boot, WAD selector, and gameplay HUD.
// Uses ANSI escape codes for color on the libnds text console.
//
// The sub (bottom) screen is configured as a 4BPP tiled text console
// giving a 32-column x 24-row character grid. libnds maps a subset of
// ANSI escape sequences onto this hardware, so we can position the
// cursor and set foreground colors without touching VRAM directly.
//
// Three display phases share this single console:
//   1. Boot     -- hardware checks (CPU model, FAT mount).
//   2. WAD menu -- D-pad selector when multiple IWADs are on the SD card.
//   3. Gameplay -- live HUD: FPS, frame count, zone heap, sound cache.
//
// Grid: 32 columns (0-31) x 24 rows. ANSI positions are 1-indexed.
// Every visible line must be at most 32 characters.

#include <nds.h>
#include <stdio.h>
#include <string.h>

#include "nds_panel.h"
#include "i_timer.h"
#include "z_zone.h"
#include "doomstat.h"
#include "r_state.h"
#include "r_main.h"
#include "m_fixed.h"

// ANSI color macros.
//
// libnds text console supports a small subset of ANSI SGR sequences:
//   \x1b[<fg>;{0|1}m   where <fg> is 30-37 (standard colors)
//                       and 0 = normal intensity, 1 = bright/bold.
//
// Quick reference for the escape codes used throughout this file:
//   \x1b[r;cH   -- move cursor to row r, column c (1-indexed)
//   \x1b[2J     -- clear the entire screen
//   \x1b[39;0m  -- reset to default foreground, normal intensity
//   \x1b[30-37m -- set foreground: 30=black 31=red 32=green
//                   33=yellow 34=blue 35=magenta 36=cyan 37=white
//   ;1m suffix  -- bright/bold variant of the chosen color
//
// Color scheme:
//   CW (white,  bright) -- titles, main labels, prominent values
//   CC (cyan,   bright) -- field labels ("WAD", "FPS", "Zone")
//   CG (green,  bright) -- success indicators, selected menu item
//   CR (red,    bright) -- error messages, fatal screens
//   CY (yellow, bright) -- URLs and call-to-action text
//   CD (white,  normal) -- dim separators, secondary text
//   C0 (reset)          -- return to default after each colored span
#define CW "\x1b[37;1m"
#define CC "\x1b[36;1m"
#define CG "\x1b[32;1m"
#define CR "\x1b[31;1m"
#define CY "\x1b[33;1m"
#define CD "\x1b[37;0m"
#define C0 "\x1b[39;0m"

//                 123456789012345678901234567890
#define HLINE     "=============================="
#define SLINE     "------------------------------"

#define VERSION "0.2.0"

static void at(int r)
{
	printf("\x1b[%d;1H", r);
}

// WAD description lookup
static const struct {
	const char *filename;
	const char *description;
} wad_info[] = {
	{ "doom2.wad",     "Doom II"             },
	{ "plutonia.wad",  "Final Doom: Plutonia"},
	{ "tnt.wad",       "Final Doom: TNT"     },
	{ "doom.wad",      "Doom"                },
	{ "doom1.wad",     "Doom Shareware"      },
	{ "doom2f.wad",    "Doom II (FR)"        },
	{ "chex.wad",      "Chex Quest"          },
	{ "hacx.wad",      "Hacx"                },
	{ "freedoom2.wad", "Freedoom Phase 2"    },
	{ "freedoom1.wad", "Freedoom Phase 1"    },
	{ "freedm.wad",    "FreeDM"             },
	{ NULL, NULL }
};

static const char *wad_description(const char *filename)
{
	for (int i = 0; wad_info[i].filename != NULL; i++)
		if (!strcasecmp(filename, wad_info[i].filename))
			return wad_info[i].description;
	return filename;
}

// State
static char panel_wadname[20];
static char panel_mapname[12];
static int boot_row = 5;
static boolean gameplay_initialized = false;
static boolean automap_shown = false;

// FPS tracking.
// Sampled once every 60 frames using integer math: fps = df * 1000 / dt.
// This avoids floating-point on the ARM9 and is accurate enough for a
// diagnostic display that updates roughly once per second at 60 fps.
static int frame_count = 0;
static int fps_time_prev = 0;
static int fps_frames_prev = 0;
static int fps_current = 0;

// Suppress Doom engine printf spam during loading.
// The vanilla Chocolate Doom startup prints dozens of lines of WAD lump
// enumeration, texture counts, and demo headers. On a 32x24 grid those
// scroll the boot screen off before the player can read it. Redirecting
// stdout to this no-op sink keeps the loading screen clean.
// Restored on the first gameplay frame (see NDS_Panel_DrawGameplay).
static ssize_t null_stdout(const char *ptr, size_t len)
{
	(void)ptr;
	return (ssize_t)len;
}

// ---------------------------------------------------------
// Boot
// ---------------------------------------------------------

void NDS_Panel_Init(void)
{
	boot_row = 5;
	panel_wadname[0] = '\0';
	panel_mapname[0] = '\0';
	frame_count = 0;
	fps_current = 0;
	gameplay_initialized = false;
}

void NDS_Panel_DrawBoot(void)
{
	printf("\x1b[2J");
	at(1);  printf(CD HLINE C0);
	at(2);  printf(CW "CHOCOLATE DOOM DS" CD "    v" VERSION C0);
	at(3);  printf(CD HLINE C0);
	at(23); printf(CD HLINE C0);
	at(24); printf(CD "github.com/gufranco" C0);
	boot_row = 5;
}

void NDS_Panel_BootStatus(const char *label, const char *value, boolean ok)
{
	at(boot_row);
	printf(CC "%-9s" C0, label);
	if (ok)
		printf(CG " %.21s" C0, value);
	else
		printf(CR " %.21s" C0, value);
	boot_row++;
}

void NDS_Panel_BootError(const char *message)
{
	at(boot_row);
	printf(CR "%.31s" C0, message);
	boot_row++;
	at(21);
	printf(CD "Press START to exit." C0);
}

void NDS_Panel_DrawLoading(const char *wadname)
{
	printf("\x1b[2J");
	at(1);  printf(CD HLINE C0);
	at(2);  printf(CW "CHOCOLATE DOOM DS" CD "    v" VERSION C0);
	at(3);  printf(CD HLINE C0);

	const char *name = strrchr(wadname, '/');
	name = name ? name + 1 : wadname;
	at(5);  printf(CC "WAD " CW "%.25s" C0, name);
	at(7);  printf(CW "Loading..." C0);

	consoleSetCustomStdout(null_stdout);
}

// ---------------------------------------------------------
// WAD selector
// ---------------------------------------------------------

void NDS_Panel_DrawWADMenu(char wads[][NDS_WAD_PATH_LEN], int count, int selected)
{
	printf("\x1b[2J");
	at(1);  printf(CD HLINE C0);
	at(2);  printf(CW "CHOCOLATE DOOM DS" CD "    v" VERSION C0);
	at(3);  printf(CD HLINE C0);
	at(5);  printf(CW "Select WAD:" C0);

	for (int i = 0; i < count && i < 14; i++)
	{
		const char *name = strrchr(wads[i], '/');
		name = name ? name + 1 : wads[i];
		const char *desc = wad_description(name);

		at(7 + i);
		if (i == selected)
			printf(CG "> %-27.27s" C0, desc);
		else
			printf(CD "  %-27.27s" C0, desc);
	}

	at(24); printf(CC "D-PAD" CD ":move   " CC "A" CD ":select" C0);
}

// ---------------------------------------------------------
// Gameplay HUD
// ---------------------------------------------------------

void NDS_Panel_SetWAD(const char *wadname)
{
	const char *name = strrchr(wadname, '/');
	name = name ? name + 1 : wadname;
	strncpy(panel_wadname, name, sizeof(panel_wadname) - 1);
	panel_wadname[sizeof(panel_wadname) - 1] = '\0';
}

void NDS_Panel_SetMap(const char *mapname)
{
	strncpy(panel_mapname, mapname, sizeof(panel_mapname) - 1);
	panel_mapname[sizeof(panel_mapname) - 1] = '\0';
}

void NDS_Panel_DrawGameplay(void)
{
	frame_count++;

	// While the automap is shown on the bottom screen it redraws
	// itself every frame; the periodic HUD refresh must not clear it.
	if (automap_shown)
		return;

	if (frame_count % 60 != 0)
		return;

	if (!gameplay_initialized)
	{
		// First gameplay refresh: restore normal stdout so any later
		// printf calls (debug, errors) reach the console again.
		consoleSetCustomStdout(NULL);
		consoleClear();
		gameplay_initialized = true;
	}

	int now = I_GetTimeMS();
	if (fps_time_prev > 0 && now > fps_time_prev)
	{
		int df = frame_count - fps_frames_prev;
		int dt = now - fps_time_prev;
		fps_current = df * 1000 / dt;
	}
	fps_frames_prev = frame_count;
	fps_time_prev = now;

	int sec = now / 1000;
	int mm = sec / 60;
	int ss = sec % 60;

	int zfree = Z_FreeMemory() / 1024;
	int ztotal = Z_ZoneSize() / 1024;
	int snd = NDS_SoundCacheCount();

	// Header (rows 1-3)
	at(1);  printf(CD HLINE C0);
	at(2);  printf(CW "CHOCOLATE DOOM DS" CD "    v" VERSION C0);
	at(3);  printf(CD HLINE C0);

	// Info (rows 4-5)
	at(4);  printf(CC "WAD " CW "%-25.25s" C0, panel_wadname);
	at(5);  printf(CC "MAP " CW "%-25.25s" C0, panel_mapname);

	// Separator (row 6)
	at(6);  printf(CD SLINE C0);

	// Status (rows 7-9)
	at(7);  printf(CC "STATUS" C0);
	at(8);  printf(CC "FPS " CW "%-3d " CC "Frame " CW "%-9d" C0, fps_current, frame_count);
	at(9);  printf(CC "Time " CW "%d:%02d                     " C0, mm, ss);

	// Separator (row 10)
	at(10); printf(CD SLINE C0);

	// Memory (rows 11-13)
	at(11); printf(CC "MEMORY" C0);
	at(12); printf(CC "Zone " CW "%dK" CD "/" CW "%dK" CD " free          " C0, zfree, ztotal);
	at(13); printf(CC "Snd  " CW "%d" CD "/128 cached          " C0, snd);

	// Separator (row 14)
	at(14); printf(CD SLINE C0);

	// Bug report (rows 15-17)
	at(15); printf(CY "REPORT A BUG" C0);
	at(16); printf(CD "github.com/gufranco/");
	at(17); printf(CD "chocolate-doom-ds/issues" C0);

	// Clear rows 18-24.
	// The boot and WAD menu phases write into this area (error messages,
	// menu items, footer hints). Without clearing, stale text from those
	// earlier phases would remain visible behind the gameplay HUD.
	for (int r = 18; r <= 24; r++)
	{
		at(r);
		printf("                              ");
	}
}

// ---------------------------------------------------------
// Automap (bottom screen, SELECT toggles)
// ---------------------------------------------------------

// The automap is drawn on the 32x24 text console as an ASCII grid. Each
// frame we rebuild a character buffer by projecting every LineDef of the
// current level into the player's view, then blit it. This is a lightweight
// minimap, not the full Doom automap: it shows wall layout and the player
// arrow, which is enough to navigate on the DS.

// Grid dimensions (must fit the 32x24 console; rows 1-3 are the header).
#define MAP_COLS 32
#define MAP_ROWS 21
#define MAP_X0 0
#define MAP_Y0 4

// World extent shown around the player, in fixed-point map units.
// 1024 map units (half-width) gives a ~2048-unit-wide window, enough to
// navigate a room or corridor without the map becoming a solid blob.
#define MAP_HALF_W (1024 * FRACBITS)
#define MAP_HALF_H (768 * FRACBITS)

boolean NDS_Panel_GameplayActive(void)
{
    return gameplay_initialized;
}

void NDS_Panel_SetAutomap(boolean active)
{
    automap_shown = active;
}

boolean NDS_Panel_AutomapActive(void)
{
    return automap_shown;
}

// Force the next gameplay HUD refresh to happen immediately (used when the
// automap closes so stale map text is cleared without a one-second lag).
void NDS_Panel_ForceGameplayRedraw(void)
{
    frame_count = 0;
}

// Project a world point relative to the player into grid coordinates.
// Returns false if the point is outside the visible window.
static boolean map_project(fixed_t wx, fixed_t wy,
                           int *gx, int *gy)
{
    // Translate into the player's local frame (rotate by -viewangle).
    fixed_t dx = wx - viewx;
    fixed_t dy = wy - viewy;

    // Rotate: forward = +x_local, right = +y_local (Doom y grows south).
    // Using viewsin/viewcos which are for the full viewangle.
    fixed_t lx = FixedMul(dx, viewcos) + FixedMul(dy, viewsin);
    fixed_t ly = FixedMul(dy, viewcos) - FixedMul(dx, viewsin);

    // Clamp to the visible half-extents.
    if (lx < -MAP_HALF_W || lx > MAP_HALF_W ||
        ly < -MAP_HALF_H || ly > MAP_HALF_H)
        return false;

    // Map local coordinates to grid. lx is "forward" (up on screen, row 0),
    // ly is "right" (column increases to the right).
    *gx = MAP_X0 + (int)(((long long)ly + MAP_HALF_W) * MAP_COLS / (2 * MAP_HALF_W));
    *gy = MAP_Y0 + (int)(((long long)(MAP_HALF_H - lx)) * MAP_ROWS / (2 * MAP_HALF_H));
    return true;
}

void NDS_Panel_DrawAutomap(void)
{
    if (!gameplay_initialized)
        return;
    if (gamestate != GS_LEVEL)
        return;

    static char grid[MAP_ROWS][MAP_COLS + 1];

    // Clear grid with spaces.
    for (int r = 0; r < MAP_ROWS; r++)
    {
        for (int c = 0; c < MAP_COLS; c++)
            grid[r][c] = ' ';
        grid[r][MAP_COLS] = '\0';
    }

    // Draw every LineDef as a set of points between its two vertices.
    for (int i = 0; i < numlines; i++)
    {
        line_t *ln = &lines[i];
        int x0, y0, x1, y1;
        if (!map_project(ln->v1->x, ln->v1->y, &x0, &y0))
            continue;
        if (!map_project(ln->v2->x, ln->v2->y, &x1, &y1))
            continue;

        // Bresenham line between the two projected endpoints.
        int dx = abs(x1 - x0), dy = abs(y1 - y0);
        int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
        int err = dx - dy;
        int cx = x0, cy = y0;
        for (;;)
        {
            if (cx >= MAP_X0 && cx < MAP_X0 + MAP_COLS &&
                cy >= MAP_Y0 && cy < MAP_Y0 + MAP_ROWS)
                grid[cy - MAP_Y0][cx - MAP_X0] = '#';
            if (cx == x1 && cy == y1)
                break;
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; cx += sx; }
            if (e2 <  dx) { err += dx; cy += sy; }
        }
    }

    // Player arrow at the center, pointing in the view direction.
    // viewsin/viewcos are fixed_t; their sign gives the facing octant.
    int pcx = MAP_X0 + MAP_COLS / 2;
    int pcy = MAP_Y0 + MAP_ROWS / 2;
    char arrow = '^';
    if (abs(viewcos) >= abs(viewsin))
        arrow = (viewcos > 0) ? '>' : '<';
    else
        arrow = (viewsin > 0) ? 'v' : '^';
    if (pcy >= MAP_Y0 && pcy < MAP_Y0 + MAP_ROWS)
        grid[pcy - MAP_Y0][pcx - MAP_X0] = arrow;

    // Blit to the console.
    printf("\x1b[2J");
    at(1);  printf(CD HLINE C0);
    at(2);  printf(CW "AUTOMAP" C0);
    at(3);  printf(CD HLINE C0);

    for (int r = 0; r < MAP_ROWS; r++)
    {
        at(MAP_Y0 + r);
        printf(CG "%s" C0, grid[r]);
    }

    at(MAP_Y0 + MAP_ROWS + 1); printf(CD SLINE C0);
    at(MAP_Y0 + MAP_ROWS + 2); printf(CD CC "L/R" CD ":weapon   " CC "SELECT" CD ":close" C0);
}

// ---------------------------------------------------------
// Fatal error
// ---------------------------------------------------------

void NDS_Panel_FatalError(const char *error)
{
	consoleSetCustomStdout(NULL);
	printf("\x1b[2J");
	at(1);  printf(CD HLINE C0);
	at(2);  printf(CR "FATAL ERROR" C0);
	at(3);  printf(CD HLINE C0);
	at(5);  printf(CW "%.31s" C0, error);
	at(20); printf(CD HLINE C0);
	at(21); printf(CY "REPORT A BUG" C0);
	at(22); printf(CD "github.com/gufranco/");
	at(23); printf(CD "chocolate-doom-ds/issues" C0);
}
