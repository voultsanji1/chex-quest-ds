// Public API for the NDS bottom screen panel.
//
// The panel drives the sub (bottom) display through three phases:
//   1. Boot    -- hardware detection results, FAT filesystem status.
//   2. WAD     -- interactive WAD selection menu when multiple IWADs exist.
//   3. Gameplay -- live HUD showing FPS, frame count, zone memory, sound cache.
//
// Each phase clears the screen and redraws from scratch. The 32x24 text
// console limits every line to 32 visible characters.

#ifndef NDS_PANEL_H
#define NDS_PANEL_H

#include "doomtype.h"

// Maximum path length for a WAD file path, e.g. "/doom/freedoom2.wad".
// 64 bytes covers the /doom/ prefix plus any standard IWAD filename.
#define NDS_WAD_PATH_LEN 64

// Maximum number of WADs the menu can display.
// 16 is well above the number of known IWADs and fits the 14-row menu area.
#define NDS_MAX_WADS 16

// Reset panel state. Called once at startup before any drawing.
void NDS_Panel_Init(void);

// Draw the boot screen header with version string and separator lines.
void NDS_Panel_DrawBoot(void);

// Print one boot status line: label in cyan, value colored green or red.
void NDS_Panel_BootStatus(const char *label, const char *value, boolean ok);

// Print a red error message on the boot screen and advance the row cursor.
void NDS_Panel_BootError(const char *message);

// Clear the screen and show "Loading..." with the WAD name. Redirects stdout.
void NDS_Panel_DrawLoading(const char *wadname);

// Draw the interactive WAD selection menu with the current highlight.
void NDS_Panel_DrawWADMenu(char wads[][NDS_WAD_PATH_LEN], int count, int selected);

// Store the active WAD filename for the gameplay HUD.
void NDS_Panel_SetWAD(const char *wadname);

// Store the current map lump name (e.g. "E1M1", "MAP01") for the HUD.
void NDS_Panel_SetMap(const char *mapname);

// Redraw the gameplay HUD. Called every frame, but only refreshes every 60th.
void NDS_Panel_DrawGameplay(void);

// Display a fatal error screen with bug-report URL. Restores stdout first.
void NDS_Panel_FatalError(const char *error);

// Return the number of sound effects currently cached by the NDS mixer.
int NDS_SoundCacheCount(void);

#endif
