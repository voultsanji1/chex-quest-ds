// NDS entry point: replaces SDL's main() from upstream Chocolate Doom.
//
// This file initializes the NDS hardware, scans the SD card for WAD files,
// presents a selection menu if multiple IWADs are found, then hands control
// to D_DoomMain() which runs the normal Chocolate Doom startup and game loop.
//
// Hardware setup sequence:
//   1. Configure the sub screen as a 4BPP text console (32x24 ANSI grid).
//   2. Mount the FAT filesystem on the SD card via fatInitDefault().
//   3. Scan /doom/ for files matching known IWAD filenames.
//   4. Single WAD: auto-load. Multiple WADs: show a D-pad menu.
//   5. Build a fake argc/argv array and call D_DoomMain().

#include <nds.h>
#include <fat.h>
#include <filesystem.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "doomtype.h"
#include "d_iwad.h"
#include "m_argv.h"
#include "nds_panel.h"

void D_DoomMain(void);

#define DOOM_DIR "/doom/"

static const char *known_iwads[] = {
	"doom2.wad", "plutonia.wad", "tnt.wad", "doom.wad", "doom1.wad",
	"doom2f.wad", "chex.wad", "hacx.wad", "freedoom2.wad",
	"freedoom1.wad", "freedm.wad", NULL
};

// Case-insensitive match against the known IWAD list.
// WAD filenames on FAT SD cards can appear in any case depending on
// the tool that wrote them, so we compare case-insensitively.
static boolean is_known_iwad(const char *name)
{
	for (int i = 0; known_iwads[i] != NULL; i++)
	{
		if (!strcasecmp(name, known_iwads[i]))
			return true;
	}
	return false;
}

// Scan the /doom/ directory for regular files whose names match a known
// IWAD. Returns the number of WADs found (up to max). Each match is
// stored as a full path, e.g. "/doom/doom2.wad".
static int find_wads(char found[][NDS_WAD_PATH_LEN], int max)
{
	DIR *dir = opendir(DOOM_DIR);
	if (dir == NULL)
		return 0;

	int count = 0;
	struct dirent *entry;

	while ((entry = readdir(dir)) != NULL && count < max)
	{
		if (entry->d_type == DT_REG && is_known_iwad(entry->d_name)
		    && strlen(DOOM_DIR) + strlen(entry->d_name) < NDS_WAD_PATH_LEN)
		{
			snprintf(found[count], NDS_WAD_PATH_LEN, "%s%s", DOOM_DIR, entry->d_name);
			count++;
		}
	}

	closedir(dir);
	return count;
}

static int wad_menu(char wads[][NDS_WAD_PATH_LEN], int count)
{
	int selected = 0;

	while (1)
	{
		NDS_Panel_DrawWADMenu(wads, count, selected);

		swiWaitForVBlank();
		scanKeys();
		u32 keys = keysDown();

		if (keys & KEY_UP)
			selected = (selected - 1 + count) % count;
		if (keys & KEY_DOWN)
			selected = (selected + 1) % count;
		if (keys & KEY_A || keys & KEY_START)
			return selected;
	}
}

// Returns true if the given path can be opened for reading. Used to probe
// for the embedded Chex Quest files before committing to a load path.
static boolean file_exists(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (f == NULL)
		return false;
	fclose(f);
	return true;
}

// Build a path to a file sitting in the same directory as an existing
// file (e.g. /doom/chex.wad -> /doom/chex.deh).
static void sibling_path(const char *base, const char *name,
                         char *out, int out_len)
{
	const char *slash = strrchr(base, '/');
	if (slash)
	{
		int dir_len = (int)(slash - base);
		snprintf(out, out_len, "%.*s/%s", dir_len, base, name);
	}
	else
	{
		snprintf(out, out_len, "%s", name);
	}
}

int main(void)
{
	defaultExceptionHandler();
	setvbuf(stdout, NULL, _IONBF, 0);

	// Initialize the text console on the sub (bottom) screen.
	// BgType_Text4bpp selects a 4 bits-per-pixel tiled background, which
	// libnds configures as a 32x24 character grid with ANSI escape code
	// support. The last two arguments (false, true) place this console on
	// the sub engine so the top screen remains free for the 3D framebuffer.
	videoSetModeSub(MODE_0_2D);
	vramSetBankC(VRAM_C_SUB_BG);
	consoleInit(NULL, 0, BgType_Text4bpp, BgSize_T_256x256, 22, 3, false, true);

	NDS_Panel_Init();
	NDS_Panel_DrawBoot();

	// Hardware detection
	if (isDSiMode())
	{
		setCpuClock(true);
		NDS_Panel_BootStatus("Hardware", "DSi (16 MB, 134 MHz)", true);
	}
	else
	{
		NDS_Panel_BootStatus("Hardware", "DS Lite (4 MB, 67 MHz)", true);
	}

	// Mount the FAT filesystem on the SD card (or DLDI-patched slot).
	// This also enables the NitroFS driver used to read files embedded
	// directly inside the .nds ROM (see nitroFSInit below).
	boolean fat_ok = fatInitDefault();
	NDS_Panel_BootStatus("FAT", fat_ok ? "OK" : "FAILED", fat_ok);

	// Initialize NitroFS so the ROM can carry chex.wad / chex.deh inside
	// itself for a single-file, self-contained build. This is best effort:
	// on loaders that don't provide the ROM location it may fail, in which
	// case we fall back to reading the WADs from the SD card (/doom/).
	boolean nitro_ok = nitroFSInit(NULL);
	NDS_Panel_BootStatus("NitroFS", nitro_ok ? "OK" : "no", nitro_ok);

	// fatInitDefault() already installs the SD card (FAT) as the default
	// filesystem, so config and save writes land somewhere writable.
	// NitroFS is read-only and is reached only via explicit "nitro:/..."
	// paths, so there is nothing else to mount here.

	// Decide which IWAD to load. Prefer the embedded copy so the ROM is
	// playable on its own; otherwise scan /doom/ on the SD card.
	char *iwad_path = NULL;
	char *deh_path = NULL;
	char sibling[NDS_WAD_PATH_LEN];

	if (nitro_ok && file_exists("nitro:/chex.wad"))
	{
		iwad_path = "nitro:/chex.wad";
	}
	else
	{
		char wads[NDS_MAX_WADS][NDS_WAD_PATH_LEN];
		int wad_count = find_wads(wads, NDS_MAX_WADS);

		if (wad_count == 0)
		{
			NDS_Panel_BootError("No WAD files found.");
			NDS_Panel_BootError("");
			NDS_Panel_BootError("Put chex.wad in /doom/");
			NDS_Panel_BootError("or use the bundled ROM.");
			while (1)
			{
				swiWaitForVBlank();
				scanKeys();
				if (keysDown() & KEY_START)
					return 1;
			}
		}

		if (wad_count == 1)
		{
			iwad_path = wads[0];
		}
		else
		{
			int choice = wad_menu(wads, wad_count);
			iwad_path = wads[choice];
		}
	}

	// Locate the Chex Quest dehacked patch (Chex-specific strings and
	// behaviour). Prefer the embedded copy, then a sibling of the IWAD.
	if (nitro_ok && file_exists("nitro:/chex.deh"))
	{
		deh_path = "nitro:/chex.deh";
	}
	else if (iwad_path != NULL)
	{
		sibling_path(iwad_path, "chex.deh", sibling, sizeof(sibling));
		if (file_exists(sibling))
			deh_path = sibling;
	}

	const char *name = strrchr(iwad_path, '/');
	name = name ? name + 1 : iwad_path;
	NDS_Panel_BootStatus("WAD", name, true);

	NDS_Panel_SetWAD(iwad_path);
	NDS_Panel_DrawLoading(iwad_path);

	// Chocolate Doom expects a standard argc/argv command line. We
	// synthesize one: {"chexquest", "-iwad", "<path>"[, "-deh", "<path>"]}.
	// myargc/myargv are Doom globals that replace the real argc/argv
	// throughout the engine.
	static char *nds_argv[8];
	int argc = 0;
	nds_argv[argc++] = "chexquest";
	nds_argv[argc++] = "-iwad";
	nds_argv[argc++] = iwad_path;
	if (deh_path != NULL)
	{
		nds_argv[argc++] = "-deh";
		nds_argv[argc++] = deh_path;
	}
	nds_argv[argc] = NULL;
	myargc = argc;
	myargv = nds_argv;

	D_DoomMain();

	return 0;
}
