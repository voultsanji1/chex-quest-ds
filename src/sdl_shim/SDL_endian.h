// Byte-order conversion shim.
//
// NDS ARM9 is little-endian. LE swaps are identity (no-op). BE swaps
// reverse bytes for WAD lump headers and MIDI data which use big-endian
// format. Inline for performance since these are called during WAD
// parsing.

#ifndef SDL_ENDIAN_H_SHIM
#define SDL_ENDIAN_H_SHIM

// SDL defines byte order constants so code can branch at compile time.
// NDS is always LE, so SDL_BYTEORDER == SDL_LIL_ENDIAN.
#define SDL_LIL_ENDIAN 1234
#define SDL_BIG_ENDIAN 4321
#define SDL_BYTEORDER SDL_LIL_ENDIAN

// LE swap macros: identity on a little-endian CPU. Chocolate Doom calls
// these when reading multi-byte fields from WAD lumps that are stored
// in native (little-endian) order.
#define SDL_SwapLE16(x) (x)
#define SDL_SwapLE32(x) (x)

// BE swap: reverses two bytes. Used by mus2mid.c when parsing MIDI
// file headers, which store lengths and chunk types in big-endian.
static inline unsigned short SDL_SwapBE16(unsigned short x)
{
	return (x >> 8) | (x << 8);
}

// BE swap: reverses four bytes. Same use case as SwapBE16 but for
// 32-bit fields in MIDI headers (e.g. chunk length).
static inline unsigned int SDL_SwapBE32(unsigned int x)
{
	return ((x >> 24) & 0xFF) |
	       ((x >> 8)  & 0xFF00) |
	       ((x << 8)  & 0xFF0000) |
	       ((x << 24) & 0xFF000000);
}

#endif
