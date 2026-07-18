//
// i_music_nds.c -- NDS music API wrapper
//
// Chocolate Doom's s_sound.c calls the high-level I_* music functions
// (I_InitMusic, I_PlaySong, ...). On desktop these live in i_sound.c and
// forward to whichever music module was selected at runtime.
//
// On NDS we always use the OPL (Adlib/SB) module, which drives the
// software OPL3 emulator via the opl_nds driver. This file provides the
// I_* wrappers that forward to music_opl_module.

#include "config.h"
#include "doomtype.h"
#include "i_sound.h"
#include "z_zone.h"

extern const music_module_t music_opl_module;

void I_InitMusic(void)
{
    if (music_opl_module.Init != NULL)
        music_opl_module.Init();
}

void I_ShutdownMusic(void)
{
    if (music_opl_module.Shutdown != NULL)
        music_opl_module.Shutdown();
}

void I_SetMusicVolume(int volume)
{
    if (music_opl_module.SetMusicVolume != NULL)
        music_opl_module.SetMusicVolume(volume);
}

void I_PauseSong(void)
{
    if (music_opl_module.PauseMusic != NULL)
        music_opl_module.PauseMusic();
}

void I_ResumeSong(void)
{
    if (music_opl_module.ResumeMusic != NULL)
        music_opl_module.ResumeMusic();
}

void *I_RegisterSong(void *data, int len)
{
    if (music_opl_module.RegisterSong != NULL)
        return music_opl_module.RegisterSong(data, len);
    return NULL;
}

void I_UnRegisterSong(void *handle)
{
    if (music_opl_module.UnRegisterSong != NULL)
        music_opl_module.UnRegisterSong(handle);
}

void I_PlaySong(void *handle, boolean looping)
{
    if (music_opl_module.PlaySong != NULL)
        music_opl_module.PlaySong(handle, looping);
}

void I_StopSong(void)
{
    if (music_opl_module.StopSong != NULL)
        music_opl_module.StopSong();
}

boolean I_MusicIsPlaying(void)
{
    if (music_opl_module.MusicIsPlaying != NULL)
        return music_opl_module.MusicIsPlaying();
    return false;
}
