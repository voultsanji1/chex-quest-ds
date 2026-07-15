// ==========================================================================
// i_stubs_nds.c -- Stub Implementations for Unsupported Subsystems
//
// Chocolate Doom is built as a modular codebase where each subsystem
// (joystick, network, file discovery, etc.) has a platform-specific
// implementation. On desktop, SDL provides these. On NDS, several
// subsystems have no hardware equivalent or no meaningful implementation.
//
// Rather than scattering #ifdef NDS throughout the Chocolate Doom source,
// we provide no-op stubs here. The linker resolves the symbols, the
// functions do nothing, and the engine continues without those features.
//
// Each stub group below explains why the subsystem does not apply to NDS.
// ==========================================================================

#include <stddef.h>
#include "config.h"
#include "doomtype.h"
#include "d_event.h"
#include "i_joystick.h"
#include "net_io.h"

// -----------------------------------------------------------------------
// Joystick stubs
//
// The NDS has no analog stick, rumble motor, or external controller port
// in its standard hardware configuration. D-pad and button input is
// handled entirely by i_input_nds.c using libnds key scanning.
//
// These functions are declared in i_joystick.h and called during Doom's
// init/shutdown and each tic. The stubs prevent linker errors and ensure
// the joystick polling path is a no-op.
// -----------------------------------------------------------------------
void I_InitJoystick(void) {}
void I_ShutdownJoystick(void) {}
void I_UpdateJoystick(void) {}
void I_BindJoystickVariables(void) {}

// -----------------------------------------------------------------------
// Mouse acceleration stubs
//
// On desktop, Chocolate Doom applies an acceleration curve to raw mouse
// movement based on these variables. On NDS, touch screen input is
// translated directly to mouse events in i_input_nds.c with a fixed 8x
// scaling factor. There is no acceleration curve because the touch screen
// is an absolute positioning device used for relative deltas, not a
// mouse with variable speed.
//
// I_BindInputVariables registers these with the config system. It is
// empty because there are no NDS-specific input config knobs.
// -----------------------------------------------------------------------
void I_BindInputVariables(void) {}
int mouse_acceleration = 0;
int mouse_threshold = 0;

// -----------------------------------------------------------------------
// ENDOOM screen stub
//
// On DOS, Doom displays a text-mode farewell screen (the ENDOOM lump
// from the WAD) when the player quits. Chocolate Doom preserves this
// behavior on desktop terminals. On NDS, the bottom screen is used for
// the status panel and there is no text-mode exit sequence. The data
// parameter points to the raw ENDOOM lump, which we simply discard.
// -----------------------------------------------------------------------
void I_Endoom(byte *data) { (void)data; }

// -----------------------------------------------------------------------
// Network stubs
//
// Chocolate Doom supports multiplayer via SDL_net. The NDS build does
// not link SDL_net and does not include network play support. These
// stubs provide the net_module_t structure and the GUI wait function
// that the network initialization code references.
//
// net_sdl_module is a vtable of function pointers (init, send, receive,
// etc.). All NULL entries cause the network subsystem to recognize that
// no transport is available and skip multiplayer initialization.
// -----------------------------------------------------------------------
net_module_t net_sdl_module = { NULL, NULL, NULL, NULL, NULL };

// NET_WaitForLaunch is called by the multiplayer lobby code (net_gui.c)
// to display a "waiting for players" screen using SDL GUI widgets. On
// NDS this path is never reached, but the stub prevents a linker error.
void NET_WaitForLaunch(void) {}

// -----------------------------------------------------------------------
// File enumeration stubs (glob)
//
// On desktop, I_StartMultiGlob and I_NextGlob are used for WAD file
// auto-discovery: scanning directories for *.wad files to present in
// a file picker or IWAD selection dialog. On NDS, WAD files are loaded
// from a fixed path (/doom/ on the SD card) configured in i_main_nds.c.
// There is no directory browsing UI.
//
// I_StartMultiGlob returns NULL (no glob handle), I_NextGlob returns
// NULL (no more results), and I_EndGlob does nothing. This causes the
// auto-discovery code to find zero files and fall through to the
// explicit path logic.
// -----------------------------------------------------------------------
#include "i_glob.h"
#include <stdarg.h>
glob_t *I_StartMultiGlob(const char *directory, int flags,
                          const char *glob, ...)
{
	(void)directory; (void)flags; (void)glob;
	return NULL;
}

const char *I_NextGlob(glob_t *glob)
{
	(void)glob;
	return NULL;
}

void I_EndGlob(glob_t *glob)
{
	(void)glob;
}
