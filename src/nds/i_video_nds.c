// NDS Video Subsystem
//
// Renders Doom's native 320x200 framebuffer into an 8-bit paletted software
// buffer (I_VideoBuffer), then copies it each frame into VRAM where the NDS
// hardware affine transform scales it to the top screen's 256x192 resolution.
//
// Memory layout:
//   VRAM_A (128 KB, mapped at 0x06000000) + VRAM_B (128 KB, at 0x06020000)
//   together provide 256 KB of contiguous background memory. The hardware
//   bitmap mode BG_BMP8_512x256 treats this as a single 512x256 8-bit
//   indexed-color framebuffer. Each row occupies 512 bytes (the hardware
//   stride), but only the first 320 bytes per row contain active pixel data.
//   Rows beyond 200 are unused.
//
// Scaling:
//   The NDS BG affine registers PA and PD are fixed-point 8.8 values that
//   control horizontal and vertical scaling respectively.
//     PA = source_width  * 256 / screen_width  = 320 * 256 / 256 = 320
//     PD = source_height * 256 / screen_height = 200 * 256 / 192 = 266
//   The hardware reads PA source texels for every 256 screen pixels
//   horizontally, and PD source texels for every 256 screen pixels
//   vertically, producing the shrink from 320x200 to 256x192.
//
// Frame upload:
//   Each frame, I_FinishUpdate copies 320 bytes per row from the software
//   buffer into VRAM, advancing the destination pointer by 512 bytes per row
//   to match the hardware stride. DC_FlushRange ensures the ARM9 data cache
//   is written back to main memory so the DMA controller (driven by the ARM7
//   coprocessor) reads current pixel data. dmaCopy performs the actual
//   hardware DMA transfer, which is faster than a CPU memcpy for aligned
//   block moves.

#include <nds.h>
#include <string.h>

#include "config.h"
#include "doomtype.h"
#include "i_video.h"
#include "nds_panel.h"
#include "m_config.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"

// Global variables expected by Chocolate Doom
pixel_t *I_VideoBuffer = NULL;
char *video_driver = "";
boolean screenvisible = true;
int vanilla_keyboard_mapping = 1;
boolean screensaver_mode = false;
int usegamma = 0;
int screen_width = SCREENWIDTH;
int screen_height = SCREENHEIGHT;
int fullscreen = 0;
int aspect_ratio_correct = 0;
int integer_scaling = 0;
int smooth_pixel_scaling = 0;
int vga_porch_flash = 0;
int force_software_renderer = 1;
int png_screenshots = 0;
char *window_position = "";
unsigned int joywait = 0;
int usemouse = 0;

static u8 *vram_fb = NULL;

void I_InitGraphics(void)
{
	// Allocate the video buffer (320x200 8-bit paletted)
	I_VideoBuffer = Z_Malloc(SCREENWIDTH * SCREENHEIGHT, PU_STATIC, NULL);
	memset(I_VideoBuffer, 0, SCREENWIDTH * SCREENHEIGHT);

	// Top screen: MODE_5_2D with BG3 as 8-bit bitmap
	videoSetMode(MODE_5_2D | DISPLAY_BG3_ACTIVE);

	// Map VRAM banks A and B to main BG memory, providing 256 KB of
	// contiguous framebuffer space starting at 0x06000000.
	vramSetBankA(VRAM_A_MAIN_BG_0x06000000);
	vramSetBankB(VRAM_B_MAIN_BG_0x06020000);

	// BG_BMP8_512x256: the hardware framebuffer is 512 bytes wide (the
	// stride) and 256 rows tall, using 8-bit indexed color. Only the
	// first 320 columns and 200 rows contain active Doom pixels.
	REG_BG3CNT = BG_BMP8_512x256 | BG_BMP_BASE(0);

	// Affine transform registers: scale 320x200 source to 256x192 screen.
	// PA and PD are fixed-point 8.8 values. The hardware divides source
	// texel coordinates by 256, so:
	//   PA = 320 * 256 / 256 = 320  (horizontal: 320 src -> 256 dst)
	//   PD = 200 * 256 / 192 = 266  (vertical:   200 src -> 192 dst)
	// PB and PC control rotation/shear and are zero for axis-aligned scaling.
	REG_BG3PA = (320 * 256) / 256;
	REG_BG3PB = 0;
	REG_BG3PC = 0;
	REG_BG3PD = (200 * 256) / 192;
	REG_BG3X = 0;
	REG_BG3Y = 0;

	vram_fb = (u8 *)BG_GFX;

	// Clear VRAM so screen starts black
	memset(vram_fb, 0, 512 * 256);

	// Default grayscale palette until PLAYPAL is loaded.
	// RGB15: 5 bits per channel (0-31), packed into a 16-bit value.
	// BIT(15) is the enable bit; without it the color is treated as
	// transparent by the hardware.
	for (int i = 0; i < 256; i++)
	{
		int gray = i >> 3;
		BG_PALETTE[i] = RGB15(gray, gray, gray) | BIT(15);
	}

	screenvisible = true;
}

// Video subsystem stubs (not used on NDS, required by Chocolate Doom interface)
//
// Chocolate Doom's platform abstraction layer defines these functions for
// window management, mouse grabbing, icon loading, and other features that
// only apply to desktop platforms with SDL. On the NDS they are no-ops, but
// the linker requires definitions because the shared Doom code calls them
// unconditionally.
void I_ShutdownGraphics(void) {}
void I_GraphicsCheckCommandLine(void) {}
void I_SetWindowTitle(const char *title) { (void)title; }
void I_CheckIsScreensaver(void) {}
void I_SetGrabMouseCallback(grabmouse_callback_t func) { (void)func; }
void I_DisplayFPSDots(boolean dots_on) { (void)dots_on; }
void I_BindVideoVariables(void) {}
void I_InitWindowTitle(void) {}
void I_RegisterWindowIcon(const unsigned int *icon, int width, int height)
	{ (void)icon; (void)width; (void)height; }
void I_InitWindowIcon(void) {}
void I_EnableLoadingDisk(int xoffs, int yoffs) { (void)xoffs; (void)yoffs; }
void I_BeginRead(void) {}

void I_GetWindowPosition(int *x, int *y, int w, int h)
	{ *x = 0; *y = 0; (void)w; (void)h; }

// I_SetPalette: convert Doom's 24-bit RGB palette to NDS 15-bit format.
// Doom provides 256 entries as sequential R, G, B bytes (8 bits each).
// The NDS hardware palette stores RGB15 values: 5 bits per channel (0-31),
// packed into 16 bits. BIT(15) enables the color (transparency bit).
void I_SetPalette(byte *palette)
{
	for (int i = 0; i < 256; i++)
	{
		int r = palette[i * 3 + 0] >> 3;
		int g = palette[i * 3 + 1] >> 3;
		int b = palette[i * 3 + 2] >> 3;
		BG_PALETTE[i] = RGB15(r, g, b) | BIT(15);
	}
}

// I_GetPaletteIndex: find the closest palette entry for an RGB color.
// This is called rarely (e.g., for crosshair color lookup). Returning 0
// (black) is an acceptable stub since the crosshair is not critical on NDS.
int I_GetPaletteIndex(int r, int g, int b)
{
	(void)r; (void)g; (void)b;
	return 0;
}

void I_UpdateNoBlit(void) {}

void I_FinishUpdate(void)
{
	if (I_VideoBuffer == NULL || vram_fb == NULL)
		return;

	// Copy 320x200 software buffer to VRAM with 512-byte hardware stride.
	// For each row:
	//   1. DC_FlushRange: flush the ARM9 data cache for this row so the
	//      DMA controller (which reads from main memory, not the cache)
	//      sees the current pixel data.
	//   2. dmaCopy: hardware DMA copies 320 bytes (one row of pixels).
	//   3. Advance dest by 512 to skip to the next row in the hardware
	//      framebuffer (the stride is 512, but only 320 bytes are active).
	u8 *dest = vram_fb;
	pixel_t *src = I_VideoBuffer;

	for (int y = 0; y < SCREENHEIGHT; y++)
	{
		DC_FlushRange(src, SCREENWIDTH);
		dmaCopy(src, dest, SCREENWIDTH);
		src += SCREENWIDTH;
		dest += 512;
	}

	// Update the bottom (touch) screen with the status bar, automap,
	// or debug overlay after the top screen framebuffer is uploaded.
	NDS_Panel_DrawGameplay();
}

void I_ReadScreen(pixel_t *scr)
{
	memcpy(scr, I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT);
}

void I_StartFrame(void) {}

// I_StartTic is in i_input_nds.c
