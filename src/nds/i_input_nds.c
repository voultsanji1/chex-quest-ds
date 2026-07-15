// ==========================================================================
// i_input_nds.c -- NDS Input Subsystem
//
// Translates Nintendo DS hardware buttons and touch screen input into
// Doom's internal event system. The NDS has no keyboard or mouse, so
// every physical input must be mapped to the key constants Doom expects.
//
// Button presses become ev_keydown/ev_keyup events posted to the engine
// via D_PostEvent. Touch screen drags on the bottom screen are converted
// to ev_mouse events for horizontal turning, giving the player a way to
// look left/right without analog stick hardware.
//
// This file uses an event-driven model: I_StartTic is called once per
// game tic by the main loop. It scans the current button state, computes
// pressed/released deltas against the previous frame, and posts discrete
// events. Doom's event queue then dispatches them to the appropriate
// responder (menu system, game input, automap, etc.). This matches
// Chocolate Doom's architecture, which expects D_PostEvent rather than
// direct polling of a key state array.
// ==========================================================================

#include <nds.h>

#include "config.h"
#include "doomtype.h"
#include "doomkeys.h"
#include "d_event.h"
#include "SDL.h"
#include "i_video.h"

// Chocolate Doom uses D_PostEvent for the event queue
void D_PostEvent(event_t *ev);

// Joystick configuration variables. On SDL platforms these live in
// i_joystick.c and control analog stick behavior. NDS has no analog
// stick, so these are zeroed out. They must exist because Chocolate
// Doom's configuration binding code references them.
int use_analog = 0;
int joystick_move_sensitivity = 0;
int joystick_turn_sensitivity = 0;

// -----------------------------------------------------------------------
// Button-to-key mapping table
//
// Each entry maps an NDS hardware button (libnds KEY_* constant) to the
// Doom key constant that Chocolate Doom expects for that action.
//
// Design rationale for each mapping:
//
//   D-pad UP/DOWN/LEFT/RIGHT -> arrow keys
//     Standard movement. Doom's default keyboard layout uses arrows for
//     forward, backward, and turning.
//
//   A -> KEY_RCTRL (fire weapon) + KEY_ENTER (menu confirm)
//     RCTRL is Doom's default fire key. A is the primary action button
//     on NDS, making it the natural fire trigger. It also sends ENTER
//     so the same button confirms menu selections, matching the feel
//     of console Doom ports where one button handles both roles.
//
//   B -> spacebar (use/open doors)
//     Spacebar is Doom's "use" key for opening doors, flipping switches,
//     and activating lifts. B is the secondary face button, placed where
//     the thumb naturally rests for frequent actions.
//
//   X -> KEY_RSHIFT (run modifier)
//     RSHIFT is Doom's "run" toggle. Holding X while moving doubles
//     the player's speed. X is above B on the NDS face buttons, easy
//     to hold with the thumb while pressing the D-pad.
//
//   Y -> '1' (select weapon 1, fist/chainsaw)
//     Doom uses number keys 1-9 for weapon selection. With limited
//     buttons, Y cycles to weapon slot 1. A more complete weapon
//     switch would need a wheel menu, but slot 1 is the minimal case.
//
//   L -> ',' (strafe left)
//   R -> '.' (strafe right)
//     Comma and period are Doom's default strafe keys. The shoulder
//     buttons are ideal for strafing because they can be held while
//     the thumb uses the D-pad for forward/backward/turning.
//
//   START -> KEY_ESCAPE (open/close menu)
//     Standard console convention: START opens the pause/options menu.
//
//   SELECT -> KEY_TAB (toggle automap)
//     TAB opens Doom's automap overlay. SELECT is the natural choice
//     for a secondary system function.
// -----------------------------------------------------------------------
typedef struct
{
	int nds_key;
	int doom_key;
} keymap_t;

static const keymap_t keymap[] = {
	{ KEY_UP,     KEY_UPARROW },
	{ KEY_DOWN,   KEY_DOWNARROW },
	{ KEY_LEFT,   KEY_LEFTARROW },
	{ KEY_RIGHT,  KEY_RIGHTARROW },
	{ KEY_A,      KEY_RCTRL },     // Fire
	{ KEY_B,      ' ' },           // Use/Open (space)
	{ KEY_X,      KEY_RSHIFT },    // Run
	{ KEY_Y,      '1' },           // Weapon 1
	{ KEY_L,      ',' },           // Strafe left
	{ KEY_R,      '.' },           // Strafe right
	{ KEY_START,  KEY_ESCAPE },    // Menu
	{ KEY_SELECT, KEY_TAB },       // Automap
};

#define NUM_KEYMAPS (sizeof(keymap) / sizeof(keymap[0]))

// Previous frame's button bitmask. Used to compute press/release deltas.
// On each tic, we XOR current vs previous to find state changes.
static int prev_buttons = 0;

// -----------------------------------------------------------------------
// I_StartTic -- Called once per game tic by the Doom main loop.
//
// Scans the NDS hardware buttons, computes which buttons were pressed
// or released since the last call, and posts the corresponding Doom
// events. This is the bridge between NDS hardware and Doom's
// event-driven input model.
//
// Why event-driven rather than direct polling:
//   Chocolate Doom's input architecture expects discrete press/release
//   events posted to the event queue via D_PostEvent. The menu system,
//   key bindings, and game responders all consume events from this queue.
//   Polling the key state array directly would bypass the menu system
//   and break features like key repeat, menu navigation, and save-game
//   name entry.
// -----------------------------------------------------------------------
void I_StartTic(void)
{
	scanKeys();
	int held = keysHeld();
	int pressed = held & ~prev_buttons;
	int released = prev_buttons & ~held;
	prev_buttons = held;

	event_t ev;

	for (unsigned int i = 0; i < NUM_KEYMAPS; i++)
	{
		if (pressed & keymap[i].nds_key)
		{
			ev.type = ev_keydown;
			ev.data1 = keymap[i].doom_key;
			ev.data2 = ev.data3 = 0;
			D_PostEvent(&ev);
		}
		if (released & keymap[i].nds_key)
		{
			ev.type = ev_keyup;
			ev.data1 = keymap[i].doom_key;
			ev.data2 = ev.data3 = 0;
			D_PostEvent(&ev);
		}
	}

	// A button dual-maps: RCTRL (fire) from the keymap table above,
	// plus ENTER (menu confirm) here. Both events fire on the same
	// frame so the engine sees them as simultaneous key presses.
	if (pressed & KEY_A)
	{
		ev.type = ev_keydown;
		ev.data1 = KEY_ENTER;
		ev.data2 = ev.data3 = 0;
		D_PostEvent(&ev);
	}
	if (released & KEY_A)
	{
		ev.type = ev_keyup;
		ev.data1 = KEY_ENTER;
		ev.data2 = ev.data3 = 0;
		D_PostEvent(&ev);
	}

	// -------------------------------------------------------------------
	// Touch screen as mouse input for horizontal turning
	//
	// State machine with two variables:
	//   touch_active -- whether the stylus was down on the previous frame
	//   touch_last_x -- the X pixel coordinate from the previous frame
	//
	// Behavior:
	//   1. First frame of a new touch: record position, set touch_active.
	//      No mouse event is posted because there is no previous position
	//      to compute a delta from. This prevents a spurious large delta
	//      when the player first taps the screen (the jump from "no touch"
	//      to an arbitrary X coordinate would cause a sudden spin).
	//
	//   2. Subsequent frames while dragging: compute pixel delta from the
	//      previous frame's position. Multiply by 8 to scale into Doom's
	//      mouse sensitivity range. Doom's internal mouse handling expects
	//      values in the hundreds for a noticeable turn; raw pixel deltas
	//      on the 256-pixel-wide NDS touch screen are too small without
	//      this scaling factor.
	//
	//   3. Stylus lifted: reset touch_active. Next touch starts fresh
	//      from step 1.
	//
	// Only horizontal (X axis) movement is used. ev_mouse data2 is the
	// horizontal delta (turning), data3 would be vertical (look up/down)
	// but vanilla Doom has no vertical mouselook.
	// -------------------------------------------------------------------
	static int touch_active = 0;
	static int touch_last_x = 0;
	if (held & KEY_TOUCH)
	{
		touchPosition touch;
		touchRead(&touch);
		if (touch_active)
		{
			int dx = touch.px - touch_last_x;
			if (dx != 0)
			{
				ev.type = ev_mouse;
				ev.data1 = 0;
				ev.data2 = dx * 8;
				ev.data3 = 0;
				D_PostEvent(&ev);
			}
		}
		touch_last_x = touch.px;
		touch_active = 1;
	}
	else
	{
		touch_active = 0;
	}
}

// -----------------------------------------------------------------------
// Text input stubs
//
// Chocolate Doom calls these when the save-game name entry screen is
// active on desktop platforms to enable/disable SDL text input mode.
// NDS has no software keyboard, so these are no-ops. The existing
// button-to-key mapping above is sufficient for menu navigation.
// -----------------------------------------------------------------------
void I_StartTextInput(int x1, int y1, int x2, int y2)
{
	(void)x1; (void)y1; (void)x2; (void)y2;
}

void I_StopTextInput(void) {}

// -----------------------------------------------------------------------
// SDL input handler stubs
//
// On desktop platforms, i_input.c processes SDL_Event structures from the
// SDL event loop for keyboard and mouse input. NDS does not use SDL's
// event loop for input. All input comes from libnds functions (scanKeys,
// touchRead) in I_StartTic above. These stubs satisfy the linker for
// code paths that reference the SDL input handlers.
// -----------------------------------------------------------------------
void I_HandleKeyboardEvent(SDL_Event *sdlevent) { (void)sdlevent; }
void I_HandleMouseEvent(SDL_Event *sdlevent) { (void)sdlevent; }
