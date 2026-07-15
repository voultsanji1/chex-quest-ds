// NDS Sound Subsystem
//
// Sound effects are played via libnds soundPlaySample(), which sends PCM
// data to the ARM7 coprocessor's hardware mixer. Music playback is disabled
// because the NDS lacks a hardware OPL FM synthesizer, and software OPL
// emulation at the native 49716 Hz sample rate consumes nearly 100% of the
// ARM9 CPU. Music support is stubbed out until an ARM7-side OPL
// implementation becomes viable.
//
// Sound data is cached after first use. The WAD stores sounds in DMX format
// with unsigned 8-bit PCM samples. On load, each sample is converted to
// signed 8-bit PCM (which the NDS hardware expects) and stored in Doom's
// zone allocator. The zone allocator manages a 2.5 MB pool on DS Lite,
// which is used instead of system malloc because the system heap has only
// ~280 KB free after the zone allocation. Using malloc for sound data
// caused heap exhaustion and freezes in early development.

#include <nds.h>
#include <string.h>

#include "config.h"
#include "doomtype.h"
#include "d_mode.h"
#include "i_sound.h"
#include "i_timer.h"
#include "m_config.h"
#include "w_wad.h"
#include "z_zone.h"

// Global sound config variables
int snd_sfxdevice = SNDDEVICE_SB;
int snd_musicdevice = SNDDEVICE_ADLIB;
int snd_samplerate = 11025;
int snd_cachesize = 64 * 1024;
int snd_maxslicetime_ms = 28;
char *snd_musiccmd = "";
int snd_pitchshift = 0;
int use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;
char *music_pack_path = "";
char *timidity_cfg_path = "";

// Number of sound channels
#define NUM_CHANNELS 8

// Cached sound data keyed by WAD lump number.
// lumpnum serves as the hash key: each unique sound effect in the WAD has
// a distinct lump index, and the cache uses (lumpnum % MAX_CACHED_SOUNDS)
// as the slot index for O(1) lookup.
typedef struct
{
	int lumpnum;        // WAD lump number (-1 = empty slot)
	byte *data;         // signed 8-bit PCM (converted from unsigned)
	int length;         // data length in bytes
	int samplerate;     // sample rate from WAD lump
} cached_sound_t;

// Timer-based estimate of when each channel finishes playing.
// The NDS soundPlaySample API has no "is playing" query, so we calculate
// the expected end time as: now + (sample_length / sample_rate * 1000).
// I_SoundIsPlaying checks the current time against this estimate.
static int channel_end_time[NUM_CHANNELS];

// Sound cache: open-addressing hash table indexed by (lumpnum % 128).
// Collisions overwrite the previous occupant. This is acceptable because
// a typical Doom WAD contains ~60 unique sound effects, so 128 slots
// provide roughly 2x headroom and collisions are rare.
#define MAX_CACHED_SOUNDS 128
static cached_sound_t sound_cache[MAX_CACHED_SOUNDS];

// Convert a WAD sound lump to signed PCM and cache it.
static cached_sound_t *CacheSound(sfxinfo_t *sfxinfo)
{
	int lumpnum = sfxinfo->lumpnum;
	int idx = lumpnum % MAX_CACHED_SOUNDS;

	// Direct index lookup: O(1) cache hit
	if (sound_cache[idx].lumpnum == lumpnum && sound_cache[idx].data != NULL)
		return &sound_cache[idx];

	// Load the lump
	byte *lumpdata = W_CacheLumpNum(lumpnum, PU_STATIC);
	int lumplen = W_LumpLength(lumpnum);

	if (lumplen < 8)
	{
		W_ReleaseLumpNum(lumpnum);
		return NULL;
	}

	// DMX sound format (as stored in Doom WAD files):
	//   Bytes 0-1: format code. Value 3 = unsigned 8-bit PCM.
	//   Bytes 2-3: sample rate in Hz (little-endian). Typically 11025.
	//   Bytes 4-7: data length in samples (little-endian 32-bit).
	//   Bytes 8+:  unsigned 8-bit PCM sample data.
	// Reference: https://doomwiki.org/wiki/Sound
	int format = lumpdata[0] | (lumpdata[1] << 8);
	int rate = lumpdata[2] | (lumpdata[3] << 8);
	int datalen = lumpdata[4] | (lumpdata[5] << 8) |
	              (lumpdata[6] << 16) | (lumpdata[7] << 24);

	if (format != 3 || datalen <= 0 || datalen > lumplen - 8)
	{
		W_ReleaseLumpNum(lumpnum);
		return NULL;
	}

	// Skip 16-byte padding at start and end of sample data.
	// The DMX format pads each sample with 16 zero bytes at both ends.
	// These are ramp-up/ramp-down guards that prevent clicks in the
	// original DOS mixer. We skip them to avoid audible silence.
	byte *srcdata = lumpdata + 8 + 16;
	int srclen = datalen - 32;
	if (srclen <= 0)
	{
		srcdata = lumpdata + 8;
		srclen = datalen;
	}

	// Allocate from Doom's zone allocator instead of system malloc.
	// The zone pool is 2.5 MB on DS Lite; the system heap has only ~280 KB
	// free after the zone allocation. Using malloc here caused heap
	// exhaustion and freezes in early development.
	byte *converted = Z_Malloc(srclen, PU_STATIC, NULL);
	if (converted == NULL)
	{
		W_ReleaseLumpNum(lumpnum);
		return NULL;
	}

	// Convert unsigned 8-bit PCM to signed 8-bit PCM.
	// XOR 0x80 flips the high bit, converting the unsigned range (0-255,
	// center at 128) to the signed range (-128 to 127, center at 0).
	// The NDS hardware mixer expects signed samples.
	for (int i = 0; i < srclen; i++)
		converted[i] = srcdata[i] ^ 0x80;

	// Flush the ARM9 data cache so the ARM7 coprocessor (which drives
	// the sound hardware and reads from main memory) sees the converted
	// sample data.
	DC_FlushRange(converted, srclen);

	W_ReleaseLumpNum(lumpnum);

	// Free previous occupant if slot is taken by a different lump
	if (sound_cache[idx].data != NULL)
		Z_Free(sound_cache[idx].data);

	// Store in cache with lumpnum as the lookup key
	sound_cache[idx].lumpnum = lumpnum;
	sound_cache[idx].data = converted;
	sound_cache[idx].length = srclen;
	sound_cache[idx].samplerate = rate;

	return &sound_cache[idx];
}

void I_InitSound(GameMission_t mission)
{
	(void)mission;
	soundEnable();
	memset(channel_end_time, 0, sizeof(channel_end_time));

	for (int i = 0; i < MAX_CACHED_SOUNDS; i++)
	{
		sound_cache[i].lumpnum = -1;
		sound_cache[i].data = NULL;
		sound_cache[i].length = 0;
		sound_cache[i].samplerate = 0;
	}
}

void I_ShutdownSound(void)
{
	for (int i = 0; i < MAX_CACHED_SOUNDS; i++)
	{
		if (sound_cache[i].data != NULL)
		{
			Z_Free(sound_cache[i].data);
			sound_cache[i].data = NULL;
			sound_cache[i].lumpnum = -1;
		}
	}
}

int I_GetSfxLumpNum(sfxinfo_t *sfxinfo)
{
	char namebuf[16];
	snprintf(namebuf, sizeof(namebuf), "ds%s", sfxinfo->name);

	int lumpnum = W_CheckNumForName(namebuf);
	if (lumpnum < 0)
		lumpnum = W_CheckNumForName(sfxinfo->name);

	return lumpnum;
}

void I_UpdateSound(void) {}

void I_UpdateSoundParams(int channel, int vol, int sep)
{
	(void)channel; (void)vol; (void)sep;
}

int I_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep, int pitch)
{
	(void)pitch;

	if (channel < 0 || channel >= NUM_CHANNELS)
		return -1;

	cached_sound_t *snd = CacheSound(sfxinfo);
	if (snd == NULL || snd->data == NULL)
		return -1;

	// Map Doom volume (0-127) to NDS volume (0-127)
	int nds_vol = vol;
	if (nds_vol > 127) nds_vol = 127;

	// Map Doom separation (0-254, 127=center) to NDS pan (0-127, 64=center)
	int nds_pan = sep / 2;
	if (nds_pan > 127) nds_pan = 127;

	soundPlaySample(snd->data, SoundFormat_8Bit, snd->length,
	                snd->samplerate, nds_vol, nds_pan, false, 0);

	// Estimate when this sound finishes playing.
	// The NDS soundPlaySample API provides no callback or status query,
	// so we calculate the expected duration from the sample length and
	// rate, then store the absolute end time in milliseconds.
	if (snd->samplerate > 0)
		channel_end_time[channel] = I_GetTimeMS() +
			(snd->length * 1000 / snd->samplerate);

	return channel;
}

void I_StopSound(int channel)
{
	if (channel >= 0 && channel < NUM_CHANNELS)
		channel_end_time[channel] = 0;
}

boolean I_SoundIsPlaying(int channel)
{
	if (channel < 0 || channel >= NUM_CHANNELS)
		return false;
	return I_GetTimeMS() < channel_end_time[channel];
}

void I_PrecacheSounds(sfxinfo_t *sounds, int num_sounds)
{
	(void)sounds; (void)num_sounds;
}

// Music stubs: all functions are no-ops.
//
// Doom's music is stored as MUS/MIDI and played through OPL2 FM synthesis.
// Software OPL emulation at the native 49716 Hz sample rate requires nearly
// 100% of the ARM9 CPU, leaving no headroom for the game loop. Music is
// disabled until an ARM7-side OPL implementation becomes viable.
void I_InitMusic(void) {}
void I_ShutdownMusic(void) {}
void I_SetMusicVolume(int volume) { (void)volume; }
void I_PauseSong(void) {}
void I_ResumeSong(void) {}

void *I_RegisterSong(void *data, int len)
{
	(void)data; (void)len;
	return NULL;
}

void I_UnRegisterSong(void *handle) { (void)handle; }
void I_PlaySong(void *handle, boolean looping) { (void)handle; (void)looping; }
void I_StopSong(void) {}
boolean I_MusicIsPlaying(void) { return false; }

void I_BindSoundVariables(void) {}
void I_InitTimidityConfig(void) {}

// OPL stubs: I_SetOPLDriverVer and I_OPL_DevMessages are called
// unconditionally by s_sound.c during startup. They must exist even when
// OPL playback is disabled, or the linker will fail with undefined
// references.
void I_SetOPLDriverVer(opl_driver_ver_t ver) { (void)ver; }
void I_OPL_DevMessages(char *result, size_t result_len)
{
	if (result_len > 0)
		result[0] = '\0';
}

int NDS_SoundCacheCount(void)
{
	int count = 0;
	for (int i = 0; i < MAX_CACHED_SOUNDS; i++)
		if (sound_cache[i].lumpnum >= 0)
			count++;
	return count;
}

// Dummy sound/music modules.
// Chocolate Doom's s_sound.c and m_menu.c reference these module structs
// for runtime driver registration. On desktop platforms they point to SDL
// or OPL implementations. On NDS we provide empty structs so the linker
// resolves the symbols, and the registration system skips them because the
// function pointers are NULL.
const sound_module_t sound_sdl_module = { NULL, 0 };
const sound_module_t sound_pcsound_module = { NULL, 0 };
const music_module_t music_sdl_module = { NULL, 0 };
const music_module_t music_opl_module = { NULL, 0 };
const music_module_t music_pack_module = { NULL, 0 };
