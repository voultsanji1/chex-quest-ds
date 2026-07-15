// SDL standard library shim.
//
// Maps SDL memory functions (SDL_malloc, SDL_free, SDL_calloc) to C
// stdlib equivalents. NDS uses newlib libc which provides these
// directly.

#ifndef SDL_STDINC_H_SHIM
#define SDL_STDINC_H_SHIM

#include <stdlib.h>
#include <string.h>

// Chocolate Doom calls SDL_qsort, SDL_free, and SDL_malloc in shared
// code. On desktop these go through SDL's allocator. On NDS they map
// straight to newlib's C stdlib, which is linked via -lc.
#define SDL_qsort qsort
#define SDL_free free
#define SDL_malloc malloc

#endif
