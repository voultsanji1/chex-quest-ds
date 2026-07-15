// ==========================================================================
// i_timer_nds.c -- NDS Timer Subsystem
//
// Provides timing services for Doom's game loop using the NDS VBlank
// interrupt. The LCD controller fires a VBlank interrupt each time it
// finishes drawing a frame, at approximately 59.8261 Hz.
//
// Previous approach (replaced):
//   An earlier implementation used two cascaded 16-bit hardware timers
//   to form a 32-bit counter. This had a race condition: reading the
//   low half and high half of the counter is not atomic. If the low
//   half overflows between the two reads, the combined value jumps
//   forward or backward by 65536 ticks. On a single-core ARM9 with
//   interrupts enabled, this caused occasional timing glitches.
//
// Current approach:
//   Count VBlank interrupts in a single 32-bit variable. The ISR
//   performs a single store instruction, which is atomic on ARM9
//   (one core, 32-bit aligned write). The main thread reads the
//   same variable with a single load, also atomic. No lock needed.
//
// Accuracy:
//   The NDS LCD runs at 59.8261 Hz, not exactly 60 Hz. We use 60
//   as the divisor in our conversion formulas. The error is less
//   than 0.3%, which is imperceptible when Doom's game logic runs
//   at 35 tics per second. Over a 10-minute play session, the
//   accumulated drift is under 2 seconds.
// ==========================================================================

#include <nds.h>

#include "config.h"
#include "i_timer.h"

// Nominal VBlank rate used for time conversions. The true hardware rate
// is 59.8261 Hz, but 60 simplifies the integer math and the error is
// negligible for gameplay purposes.
#define NDS_VBLANK_HZ 60

// VBlank interrupt counter.
// Volatile because it is written by the VBlank ISR and read by the main
// thread. On ARM9 (single core), a 32-bit aligned write is atomic, so
// no mutex or critical section is needed. The volatile qualifier ensures
// the compiler always reloads the value from memory rather than caching
// it in a register across function calls.
static volatile unsigned int vblank_count = 0;

// ISR registered with irqSet. Called by the hardware interrupt dispatcher
// once per VBlank (~60 times per second). Must be fast: just increment
// the counter and return.
static void vblank_handler(void)
{
	vblank_count++;
}

// Register the VBlank handler with libnds and enable the interrupt.
// Called once from I_Init during Doom's startup sequence.
void I_InitTimer(void)
{
	irqSet(IRQ_VBLANK, vblank_handler);
	irqEnable(IRQ_VBLANK);
}

// -----------------------------------------------------------------------
// I_GetTime -- Return the current time in Doom tics.
//
// Doom's internal clock runs at TICRATE = 35 tics per second. All game
// logic (movement, physics, AI, weapon timing) advances in fixed 35Hz
// steps.
//
// Conversion: tics = vblank_count * 35 / 60
//
// Integer division truncates, so the tic count advances in discrete
// steps. At 60 VBlanks per second producing 35 tics per second, a new
// tic occurs roughly every 1.71 VBlanks. The game loop calls this
// repeatedly and runs one simulation step each time the value increases.
// -----------------------------------------------------------------------
int I_GetTime(void)
{
	return (int)(vblank_count * TICRATE / NDS_VBLANK_HZ);
}

// -----------------------------------------------------------------------
// I_GetTimeMS -- Return the current time in milliseconds.
//
// Used by Doom for performance measurement and interpolation.
//
// Conversion: ms = vblank_count * 1000 / 60
//
// Each VBlank represents approximately 16.67 ms (1000 / 60). Integer
// division means the value steps by 16 or 17 ms per VBlank.
// -----------------------------------------------------------------------
int I_GetTimeMS(void)
{
	return (int)(vblank_count * 1000 / NDS_VBLANK_HZ);
}

// -----------------------------------------------------------------------
// I_Sleep -- Suspend execution for approximately the given milliseconds.
//
// On desktop, this calls SDL_Delay or OS sleep. On NDS, the smallest
// sleep granularity is one VBlank period (~16.67 ms). We convert the
// requested milliseconds to a frame count by dividing by 17 (rounded
// up from 16.67).
//
// swiWaitForVBlank halts the ARM9 CPU until the next VBlank interrupt
// fires. This is not a busy-wait: the CPU enters a low-power halt
// state, saving battery life. The interrupt wakes it up.
//
// Minimum sleep is always 1 frame, even if ms is 0 or 1. This prevents
// a busy-spin when Doom requests a very short delay.
// -----------------------------------------------------------------------
void I_Sleep(int ms)
{
	int frames = ms / 17;
	if (frames < 1)
		frames = 1;
	for (int i = 0; i < frames; i++)
		swiWaitForVBlank();
}

// Wait for a specific number of VBlank periods. Used by Doom's startup
// code for brief pauses (palette fades, title screen delays).
void I_WaitVBL(int count)
{
	for (int i = 0; i < count; i++)
		swiWaitForVBlank();
}
