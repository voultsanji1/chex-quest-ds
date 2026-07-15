// ==========================================================================
// i_system_nds.c -- NDS System Services
//
// Provides the platform-specific implementations of Doom's system
// interface: memory allocation for the zone heap, fatal error display,
// at-exit cleanup registration, and miscellaneous OS-level functions.
//
// Chocolate Doom's i_system.h declares these functions with the
// expectation that each platform fills in the details. On desktop, the
// SDL platform layer handles them. On NDS, we talk directly to libnds
// and the ARM9 hardware.
// ==========================================================================

#include <nds.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "config.h"
#include "doomtype.h"
#include "d_event.h"
#include "d_ticcmd.h"
#include "i_system.h"
#include "i_timer.h"
#include "m_config.h"
#include "m_misc.h"

// -----------------------------------------------------------------------
// At-exit cleanup registry
//
// Doom registers cleanup functions throughout initialization: saving the
// config file, shutting down the sound engine, releasing video resources,
// etc. On desktop, these go through the C library's atexit(). On NDS we
// use a fixed-size array because:
//   - The C library atexit on devkitARM has a small limit.
//   - We need to call them manually in I_Error before exit(1).
//   - 32 slots is more than enough. Chocolate Doom registers ~10-15.
//
// Functions are called in reverse registration order (LIFO), matching
// the standard atexit() contract.
// -----------------------------------------------------------------------
#define MAX_ATEXIT_FUNCS 32

static atexit_func_t atexit_funcs[MAX_ATEXIT_FUNCS];
static int num_atexit_funcs = 0;

void I_AtExit(atexit_func_t func, boolean run_if_error)
{
	(void)run_if_error;
	if (num_atexit_funcs < MAX_ATEXIT_FUNCS)
		atexit_funcs[num_atexit_funcs++] = func;
}

// -----------------------------------------------------------------------
// I_Init -- One-time system initialization.
//
// Called early in Doom's startup. On NDS the only work is starting the
// timer subsystem (VBlank interrupt counting). Other platform-specific
// init (video, audio, input) happens in their respective I_Init* calls.
// -----------------------------------------------------------------------
void I_Init(void)
{
	I_InitTimer();
}

// -----------------------------------------------------------------------
// I_ZoneBase -- Allocate the zone memory heap.
//
// Doom uses its own zone allocator (z_zone.c) for all dynamic memory.
// At startup it requests one large contiguous block from the OS, then
// subdivides it internally. This function provides that block.
//
// Memory budget by hardware:
//   DSi mode: 16 MB total RAM. Request 10 MB for the zone heap, leaving
//     ~6 MB for the ARM9 stack, libnds internals, the framebuffer, and
//     sound buffers.
//   DS Lite / original DS: 4 MB total RAM. Request 2.5 MB (2 MB + 512 KB),
//     leaving ~1.5 MB for system use. This is tight but sufficient for
//     Doom 1 shareware and most custom WADs.
//
// Fallback loop: if malloc fails (heap fragmentation, or another library
// already grabbed memory), reduce the request by 256 KB and retry.
// Stop if we drop below 1 MB, which is not enough for Doom to function.
// -----------------------------------------------------------------------
byte *I_ZoneBase(int *size)
{
	int heap_size;

	if (isDSiMode())
		heap_size = 10 * 1024 * 1024;
	else
		heap_size = 2 * 1024 * 1024 + 512 * 1024;

	byte *zone = NULL;
	while (heap_size > 1024 * 1024 && zone == NULL)
	{
		zone = (byte *)malloc(heap_size);
		if (zone == NULL)
			heap_size -= 256 * 1024;
	}

	if (zone == NULL)
	{
		printf("\n  Out of memory!");
		while (1) swiWaitForVBlank();
	}

	*size = heap_size;
	return zone;
}

boolean I_ConsoleStdout(void)
{
	return true;
}

// Returns a zeroed ticcmd_t for frames where no input is available.
// Doom calls this as the base command that input processing modifies.
ticcmd_t *I_BaseTiccmd(void)
{
	static ticcmd_t emptycmd;
	return &emptycmd;
}

// -----------------------------------------------------------------------
// I_Quit -- Clean shutdown.
//
// Calls all registered at-exit functions in reverse order, then exits.
// This is the normal exit path (user chose "Quit Game" from the menu).
// -----------------------------------------------------------------------
void I_Quit(void)
{
	for (int i = num_atexit_funcs - 1; i >= 0; i--)
		atexit_funcs[i]();
	exit(0);
}

// -----------------------------------------------------------------------
// I_Error -- Fatal error handler.
//
// Formats the error message, then delegates to NDS_Panel_FatalError
// which reinitializes the sub screen console and displays the error.
// The sub screen may have been in a different video mode (the status
// panel uses tile-based graphics), so the panel code calls
// consoleDemoInit() to switch back to a text console before printing.
//
// After displaying the error, we print "Press START to exit" using
// ANSI escape codes to position the cursor (\x1b[row;colH) and set
// text color (\x1b[37;0m for white). The user must acknowledge the
// error before we run cleanup and exit.
//
// At-exit functions are called here too, so config is saved even on
// a fatal error.
// -----------------------------------------------------------------------
void I_Error(const char *error, ...)
{
	va_list args;
	char buf[256];

	va_start(args, error);
	vsnprintf(buf, sizeof(buf), error, args);
	va_end(args);

	extern void NDS_Panel_FatalError(const char *);
	NDS_Panel_FatalError(buf);

	printf("\x1b[8;1H\x1b[37;0m  Press START to exit.\x1b[39;0m");

	while (1)
	{
		swiWaitForVBlank();
		scanKeys();
		if (keysDown() & KEY_START)
			break;
	}

	for (int i = num_atexit_funcs - 1; i >= 0; i--)
		atexit_funcs[i]();
	exit(1);
}

// Tactile feedback (force feedback / rumble). NDS has no rumble motor
// in the standard hardware, so this is a no-op.
void I_Tactile(int on, int off, int total)
{
	(void)on; (void)off; (void)total;
}

// Thin wrapper around realloc. Exists because Chocolate Doom routes all
// allocations through i_system so platforms can override behavior.
void *I_Realloc(void *ptr, size_t size)
{
	return realloc(ptr, size);
}

// Memory-mapped I/O access for Doom's cheat detection. Not applicable
// on NDS, always returns false.
boolean I_GetMemoryValue(unsigned int offset, void *value, int size)
{
	(void)offset; (void)value; (void)size;
	return false;
}

// Registers configuration variables for the system module. NDS has
// no system-level config variables (no joystick deadzone, no mouse
// grab toggle, etc.), so this is empty.
void I_BindVariables(void) {}

// -----------------------------------------------------------------------
// Console printing helpers
//
// These print startup information to the NDS sub screen console. The
// ANSI escape code \x1b[9;1H positions the cursor at row 9, column 1,
// placing the game description below the ChocoDoom DS banner art.
// -----------------------------------------------------------------------
void I_PrintStartupBanner(const char *gamedescription)
{
	printf("\x1b[9;1H  %s", gamedescription);
}

void I_PrintBanner(const char *text)
{
	printf("  %s\n", text);
}

void I_PrintDivider(void)
{
	printf("  ================================\n");
}
