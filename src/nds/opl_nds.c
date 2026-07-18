//
// opl_nds.c -- NDS OPL driver (software emulation)
//
// This is the NDS backend for Chocolate Doom's OPL music system. It is
// based on the structure of the upstream opl_sdl.c driver but replaces
// all SDL dependencies with libnds primitives.
//
// The Nuked OPL3 emulator (opl3.c) generates 16-bit stereo PCM samples.
// A libnds hardware timer fires periodically; on each tick we:
//   1. advance and fire any pending OPL music callbacks (MIDI events),
//   2. generate a slice of PCM from the emulator,
//   3. downmix stereo->mono and push it to the ARM7 mixer via
//      soundPlaySample() using a double-buffer (ping-pong).
//
// NOTE: Software OPL3 emulation is CPU intensive on the ARM9. This driver
// is provided as an experimental best-effort path; on real hardware it may
// require tuning of the sample rate (snd_samplerate) and slice size.

#include <nds.h>
#include <string.h>
#include <stdint.h>

#include "config.h"
#include "doomtype.h"
#include "i_sound.h"
#include "i_timer.h"
#include "opl.h"
#include "opl_internal.h"
#include "opl_queue.h"
#include "opl3.h"

static void ONDS_WriteRegister(uint16_t reg, uint8_t value);

// ---------------------------------------------------------------------------
// Emulator state
// ---------------------------------------------------------------------------

static opl3_chip chip;
static int oplsynth_inited = 0;

// ---------------------------------------------------------------------------
// Callback queue (MIDI event timing)
// ---------------------------------------------------------------------------

static opl_callback_queue_t *callback_queue = NULL;

// Current time, in microseconds, used to schedule OPL callbacks.
static uint64_t current_time_us = 0;

// How many microseconds of audio correspond to one generated slice.
static uint64_t slice_duration_us = 0;

// ---------------------------------------------------------------------------
// Audio buffer (ping-pong)
// ---------------------------------------------------------------------------

// Number of PCM samples generated per timer tick. At 11025 Hz and a 23 ms
// slice this is ~256 samples. Two slices are kept so one can be submitted to
// the mixer while the next is being filled.
#define OPL_NDS_SLICE_SAMPLES 256

// Signed 8-bit mono buffer handed to soundPlaySample (the NDS hardware
// mixer expects signed samples, as the working SFX path shows).
static uint8_t opl_nds_buf[2][OPL_NDS_SLICE_SAMPLES];
static int opl_nds_active = 0;

// Estimated time (ms) at which the currently-playing buffer finishes.
// soundPlaySample is non-blocking and offers no "is playing" query, so we
// reuse the same duration estimate the SFX path uses.
static int opl_nds_buffer_end_time = 0;

// Scratch stereo buffer from the emulator (int16, 2 channels).
static int16_t opl_nds_stereo[OPL_NDS_SLICE_SAMPLES * 2];

// ---------------------------------------------------------------------------
// OPL port writes from the music player
// ---------------------------------------------------------------------------

static uint16_t opl_nds_cur_reg = 0;

// Emulated OPL status/timer registers, mirroring opl_sdl.c so that
// OPL_Detect() (used by OPL_Init) succeeds on the software backend.
typedef struct
{
    unsigned int rate;     // timer ticks per second
    unsigned int enabled;  // non-zero if timer is enabled
    unsigned int value;    // last value written
    uint64_t expire_time;  // absolute time (us) when timer expires
} opl_nds_timer_t;

static opl_nds_timer_t opl_nds_timer1;
static opl_nds_timer_t opl_nds_timer2;

static void ONDS_WritePort(opl_port_t port, unsigned int value)
{
    switch (port)
    {
        case OPL_REGISTER_PORT:
            opl_nds_cur_reg = (uint16_t)value;
            break;
        case OPL_REGISTER_PORT_OPL3:
            opl_nds_cur_reg = (uint16_t)value | 0x100;
            break;
        case OPL_DATA_PORT:
        default:
            ONDS_WriteRegister(opl_nds_cur_reg, (uint8_t)value);
            break;
    }
}

// Dispatch a register write, handling the OPL timer control registers so
// that OPL_Detect() sees sensible status bits, and forwarding everything
// else to the emulator.
static void ONDS_WriteRegister(uint16_t reg, uint8_t value)
{
    int tics;

    switch (reg & 0xff)
    {
        case OPL_REG_TIMER1:
            opl_nds_timer1.value = value;
            if (opl_nds_timer1.enabled)
            {
                tics = 0x100 - value;
                opl_nds_timer1.expire_time =
                    current_time_us + ((uint64_t)tics * OPL_SECOND) /
                                      opl_nds_timer1.rate;
            }
            break;

        case OPL_REG_TIMER2:
            opl_nds_timer2.value = value;
            if (opl_nds_timer2.enabled)
            {
                tics = 0x100 - value;
                opl_nds_timer2.expire_time =
                    current_time_us + ((uint64_t)tics * OPL_SECOND) /
                                      opl_nds_timer2.rate;
            }
            break;

        case OPL_REG_TIMER_CTRL:
            if (value & 0x80)
            {
                opl_nds_timer1.enabled = 0;
                opl_nds_timer2.enabled = 0;
            }
            else
            {
                if ((value & 0x40) == 0)
                {
                    opl_nds_timer1.enabled = (value & 0x01) != 0;
                    if (opl_nds_timer1.enabled)
                    {
                        tics = 0x100 - opl_nds_timer1.value;
                        opl_nds_timer1.expire_time =
                            current_time_us +
                            ((uint64_t)tics * OPL_SECOND) /
                            opl_nds_timer1.rate;
                    }
                }
                if ((value & 0x20) == 0)
                {
                    opl_nds_timer2.enabled = (value & 0x02) != 0;
                    if (opl_nds_timer2.enabled)
                    {
                        tics = 0x100 - opl_nds_timer2.value;
                        opl_nds_timer2.expire_time =
                            current_time_us +
                            ((uint64_t)tics * OPL_SECOND) /
                            opl_nds_timer2.rate;
                    }
                }
            }
            break;

        default:
            OPL3_WriteRegBuffered(&chip, reg, value);
            break;
    }
}

// Advance the driver's virtual clock. Called from OPL_Delay() so that the
// emulated OPL status timers expire correctly even though the audio timer
// IRQ may not fire during a short synchronous delay.
void opl_nds_time_advance(uint64_t us)
{
    current_time_us += us;
}

static unsigned int ONDS_ReadPort(opl_port_t port)
{
    unsigned int result = 0;

    if (port == OPL_REGISTER_PORT_OPL3)
        return 0xff;

    if (opl_nds_timer1.enabled && current_time_us > opl_nds_timer1.expire_time)
    {
        result |= 0x80;   // either timer expired
        result |= 0x40;   // timer 1 expired
    }

    if (opl_nds_timer2.enabled && current_time_us > opl_nds_timer2.expire_time)
    {
        result |= 0x80;   // either timer expired
        result |= 0x20;   // timer 2 expired
    }

    return result;
}

// ---------------------------------------------------------------------------
// Callback scheduling
// ---------------------------------------------------------------------------

static void ONDS_SetCallback(uint64_t us, opl_callback_t callback, void *data)
{
    if (callback_queue != NULL)
        OPL_Queue_Push(callback_queue, callback, data, current_time_us + us);
}

static void ONDS_ClearCallbacks(void)
{
    if (callback_queue != NULL)
        OPL_Queue_Clear(callback_queue);
}

static void ONDS_AdjustCallbacks(float factor)
{
    if (callback_queue != NULL)
        OPL_Queue_AdjustCallbacks(callback_queue, current_time_us, factor);
}

static void ONDS_Lock(void) {}
static void ONDS_Unlock(void) {}

static int opl_nds_paused = 0;
static void ONDS_SetPaused(int paused)
{
    opl_nds_paused = paused;
}

// Fire any callbacks whose scheduled time has arrived.
static void ONDS_PumpCallbacks(void)
{
    opl_callback_t callback;
    void *data;

    if (callback_queue == NULL)
        return;

    while (!OPL_Queue_IsEmpty(callback_queue) &&
           OPL_Queue_Peek(callback_queue) <= current_time_us)
    {
        if (OPL_Queue_Pop(callback_queue, &callback, &data))
            callback(data);
    }
}

// ---------------------------------------------------------------------------
// Audio generation + submission
// ---------------------------------------------------------------------------

static void ONDS_GenerateAndPlay(void)
{
    int i;
    int16_t *stereo = opl_nds_stereo;
    uint8_t *out = opl_nds_buf[opl_nds_active];

    if (opl_nds_paused)
    {
        // Output silence while paused.
        memset(out, 0x80, OPL_NDS_SLICE_SAMPLES);
    }
    else
    {
        OPL3_GenerateStream(&chip, stereo, OPL_NDS_SLICE_SAMPLES);

        // Downmix stereo -> signed 8-bit mono. The NDS hardware mixer
        // expects signed samples (center 0), like the working SFX path
        // which uses XOR 0x80. Using unsigned (+0x80) made it grate.
        for (i = 0; i < OPL_NDS_SLICE_SAMPLES; ++i)
        {
            int32_t s = stereo[i * 2] + stereo[i * 2 + 1];
            s >>= 1; // average
            if (s > 127) s = 127;
            if (s < -128) s = -128;
            out[i] = (uint8_t)(s ^ 0x80);
        }
    }

    DC_FlushRange(out, OPL_NDS_SLICE_SAMPLES);

    // Submit to the ARM7 mixer. soundPlaySample is non-blocking and offers
    // no "is playing" query (same limitation as the SFX path), so we record
    // when this slice is expected to finish and never overwrite a buffer that
    // is still being played back.
    soundPlaySample(out, SoundFormat_8Bit, OPL_NDS_SLICE_SAMPLES,
                    snd_samplerate, 127, 64, false, 0);

    if (snd_samplerate > 0)
        opl_nds_buffer_end_time = I_GetTimeMS() +
            (OPL_NDS_SLICE_SAMPLES * 1000 / snd_samplerate);
}

// Timer IRQ: advance time, run callbacks, generate a slice.
static void ONDS_TimerHandler(void)
{
    current_time_us += slice_duration_us;

    ONDS_PumpCallbacks();

    // Don't overwrite the buffer the ARM7 is still playing. If a slice is
    // still in flight, skip this tick; the previous buffer keeps looping
    // silently until we can safely submit the next one.
    if (I_GetTimeMS() >= opl_nds_buffer_end_time)
    {
        ONDS_GenerateAndPlay();
        opl_nds_active ^= 1;
    }
}

// ---------------------------------------------------------------------------
// Driver init / shutdown
// ---------------------------------------------------------------------------

static int ONDS_Init(unsigned int port_base)
{
    (void)port_base;

    OPL3_Reset(&chip, snd_samplerate);

    callback_queue = OPL_Queue_Create();
    if (callback_queue == NULL)
        return 0;

    current_time_us = 0;
    slice_duration_us = (uint64_t)(1000000ULL * OPL_NDS_SLICE_SAMPLES) /
                        snd_samplerate;

    // Initialize the emulated status timers. rate is the OPL base clock
    // (input clock / 72); for the software backend we approximate using a
    // nominal 1 MHz-derived value consistent with opl_sdl.c.
    opl_nds_timer1.rate = 12500;  // ~1 MHz / 80, nominal OPL timer1 rate
    opl_nds_timer2.rate = 3125;   // ~1 MHz / 320, nominal OPL timer2 rate
    opl_nds_timer1.enabled = 0;
    opl_nds_timer1.value = 0;
    opl_nds_timer1.expire_time = 0;
    opl_nds_timer2.enabled = 0;
    opl_nds_timer2.value = 0;
    opl_nds_timer2.expire_time = 0;

    opl_nds_active = 0;
    opl_nds_paused = 0;
    oplsynth_inited = 1;

    // Start a hardware timer that fires once per audio slice.
    // The timer source clock is 33.513982 MHz; with ClockDivider_1024 it
    // ticks at ~32728 Hz. We want one tick per audio slice, i.e. a frequency
    // of (1e6 / slice_duration_us) Hz. TIMER_FREQ_1024 computes the correct
    // 16-bit reload value for a given frequency in Hz.
    timerStart(0, ClockDivider_1024,
               TIMER_FREQ_1024(1000000 / slice_duration_us),
               ONDS_TimerHandler);

    return 1;
}

static void ONDS_Shutdown(void)
{
    timerStop(0);
    if (callback_queue != NULL)
    {
        OPL_Queue_Destroy(callback_queue);
        callback_queue = NULL;
    }
    oplsynth_inited = 0;
}

opl_driver_t opl_nds_driver =
{
    "NDS",
    ONDS_Init,
    ONDS_Shutdown,
    ONDS_ReadPort,
    ONDS_WritePort,
    ONDS_SetCallback,
    ONDS_ClearCallbacks,
    ONDS_Lock,
    ONDS_Unlock,
    ONDS_SetPaused,
    ONDS_AdjustCallbacks,
};
