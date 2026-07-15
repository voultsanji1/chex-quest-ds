// Minimal SDL type shim for NDS.
//
// Chocolate Doom includes SDL.h throughout its codebase for types like
// SDL_Event, SDL_bool, and SDL_EventType. This shim provides only the
// type definitions needed to compile. No SDL library is linked.
// Real platform implementations live in src/nds/.

#ifndef SDL_H_SHIM
#define SDL_H_SHIM

#include "SDL_stdinc.h"
#include "SDL_endian.h"

// SDL_EventType: the event types Chocolate Doom actually checks for in
// its input polling loop. Only the variants referenced in i_input.c and
// d_event.c are needed. The numeric values do not matter because NDS
// input code in src/nds/i_input_nds.c builds events directly.
typedef enum
{
	SDL_KEYDOWN,
	SDL_KEYUP,
	SDL_MOUSEMOTION,
	SDL_MOUSEBUTTONDOWN,
	SDL_MOUSEBUTTONUP,
	SDL_QUIT
} SDL_EventType;

// SDL_Event: sized to match SDL's event union footprint so sizeof()
// checks in Chocolate Doom pass. The real SDL_Event is a union of
// ~20 structs totaling roughly 64 bytes. padding[16] = 16 ints = 64
// bytes, plus the type field, keeps the struct large enough that any
// sizeof(SDL_Event) comparison in the original code remains valid.
typedef struct
{
	int type;
	int padding[16];
} SDL_Event;

// SDL_bool: boolean type used in function signatures throughout
// Chocolate Doom. Maps to int under the hood, matching SDL's own
// definition (SDL_FALSE=0, SDL_TRUE=1).
typedef int SDL_JoystickID;

#endif
