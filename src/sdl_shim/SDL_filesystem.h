// Filesystem path shim.
//
// NDS has no user home directory or OS-managed preferences. SDL_GetPrefPath
// is used by Chocolate Doom to find config/save locations. We return /doom/
// on the SD card. malloc(16) is safe: "/doom/" is 6 chars + null = 7 bytes,
// 16 provides margin.

#ifndef SDL_FILESYSTEM_H_SHIM
#define SDL_FILESYSTEM_H_SHIM

#include <stdlib.h>
#include <string.h>

// Chocolate Doom calls SDL_GetPrefPath(org, app) to locate the directory
// for config files and savegames. On desktop, this returns a platform-
// specific path like ~/Library/Application Support/. On NDS, the SD card
// filesystem is flat, so we return a fixed path. The org and app
// parameters are unused.
static inline char *SDL_GetPrefPath(const char *org, const char *app)
{
	(void)org; (void)app;
	char *path = (char *)malloc(16);
	if (path) strcpy(path, "/doom/");
	return path;
}

// Chocolate Doom calls SDL_free on the pointer returned by SDL_GetPrefPath.
// Since we allocated with stdlib malloc, SDL_free maps directly to free.
#define SDL_free free

#endif
