// ARM7 core for Chocolate Doom DS.
// Based on the BlocksDS default ARM7 implementation.
// Enables MaxMod for sound effect playback via soundPlaySample().
//
// NDS dual-CPU architecture:
//   ARM9 (67 MHz, or 134 MHz on DSi): runs game logic, software
//       rendering, MIDI sequencing, and all application code.
//   ARM7 (33 MHz): owns the hardware I/O peripherals: touchscreen
//       controller, real-time clock, sound DMA, WiFi, and power
//       management. User code on the ARM7 is intentionally minimal;
//       most work is done by libnds and MaxMod interrupt handlers.
//
// Communication between the two CPUs uses the FIFO mailbox.
// ARM9 sends commands with fifoSendValue32(), ARM7 registers
// handlers via fifoSetValue32Handler(). Each FIFO channel carries
// a different subsystem's messages (sound, system, MaxMod, etc.).

#include <nds.h>
#include <maxmod7.h>

volatile bool exit_loop = false;

void power_button_callback(void)
{
	exit_loop = true;
}

void vblank_handler(void)
{
	inputGetAndSend();
}

int main(void)
{
#if defined(DEBUG_LIBS)
	defaultExceptionHandler();
#endif

	// Power on the 16 hardware PCM/ADPCM sound channels.
	// Without this call the channels stay gated and no audio reaches
	// the DAC, even if MaxMod is initialized.
	enableSound();

	// Read user settings (nickname, birthday, language, alarm) from
	// the firmware flash chip. libnds stores these in a global struct
	// that other functions (like locale detection) can query.
	readUserSettings();

	ledBlink(0);
	touchInit();

	irqInit();
	fifoInit();

	// Install the FIFO handler for libnds sound requests.
	// When ARM9 calls soundPlaySample(), it sends a FIFO message.
	// This handler on ARM7 receives it and programs the hardware
	// sound channel registers (source address, frequency, volume,
	// format, loop point).
	installSoundFIFO();

	// Install the system FIFO handler for sleep mode, storage access,
	// and firmware reads. These are low-level platform services that
	// only ARM7 can perform because it owns the relevant peripherals.
	installSystemFIFO();

	if (isDSiMode())
		installCameraFIFO();

	// Initialize MaxMod on the ARM7 side using the FIFO_MAXMOD channel.
	// MaxMod manages the 16 hardware sound channels, mixing and
	// prioritizing active effects. The playback path is:
	//   ARM9 soundPlaySample() -> FIFO message -> MaxMod ARM7
	//   -> programs DMA source/length/format -> hardware DAC -> speaker
	mmInstall(FIFO_MAXMOD);

	setPowerButtonCB(power_button_callback);

	// Start the RTC-backed clock timer. The initial read from the
	// real-time clock is slow (around 100 ms due to the serial bus),
	// so libnds reads it once here and then uses a timer ISR to
	// increment the seconds counter each tick.
	initClockIRQTimer(LIBNDS_DEFAULT_TIMER_RTC);

	irqSet(IRQ_VBLANK, vblank_handler);
	irqEnable(IRQ_VBLANK);

	while (!exit_loop)
	{
		// Check for the standard NDS homebrew shutdown combo:
		// SELECT + START + L + R held simultaneously.
		// This is a widely adopted convention across the homebrew
		// ecosystem so users have a consistent way to exit any app.
		const uint16_t key_mask = KEY_SELECT | KEY_START | KEY_L | KEY_R;
		uint16_t keys_pressed = ~REG_KEYINPUT;

		if ((keys_pressed & key_mask) == key_mask)
			exit_loop = true;

		swiWaitForVBlank();
	}

	return 0;
}
