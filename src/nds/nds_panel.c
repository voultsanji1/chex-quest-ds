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
