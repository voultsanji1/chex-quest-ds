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
#include <stdlib.h>

#include "config.h"
#include "doomtype.h"
#include "doomkeys.h"
#include "d_event.h"
#include "SDL.h"
#include "i_video.h"
#include "nds_panel.h"
#include "doomstat.h"
#include "doomdef.h"
#include "d_player.h"
#include "am_map.h"

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
//   A -> KEY_RCTRL (fire weapon)
//     RCTRL is Doom's default fire key. A is the primary action button
//     on NDS. It does NOT send ENTER, so firing never refreshes the
//     status message or phantom-confirms a menu (the old bug).
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
//   Y -> KEY_ENTER (menu confirm / save / Y-N dialog)
//     ENTER confirms menu selections and save-game slots. It is mapped
//     to a dedicated face button (not A) so shooting never triggers it.
//
//   L / R -> previous / next owned weapon
//     Doom uses number keys 1-9 for weapon selection. The shoulder
//     buttons cycle through the weapons you actually own, edge-triggered
//     so one tap = one switch.
//
//   START -> KEY_ESCAPE (open/close menu)
//     Standard console convention: START opens the pause/options menu.
//
//   SELECT -> open/close the bottom-screen weapon menu
//     SELECT toggles an on-screen weapon list drawn on the touch screen.
//     This replaces the old TAB-automap binding; weapon switching is the
//     most common action that needed more than a single button.
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
	{ KEY_A,      KEY_RCTRL },     // Fire (no ENTER: avoids message-refresh / phantom menu confirm)
	{ KEY_B,      ' ' },           // Use/Open (space)
	{ KEY_X,      KEY_RSHIFT },    // Run
	{ KEY_Y,      KEY_ENTER },     // Menu confirm / Save / Y-N dialog
	{ KEY_START,  KEY_ESCAPE },    // Menu
	{ KEY_SELECT, KEY_TAB },       // Automap (Doom's built-in overlay)
};

#define NUM_KEYMAPS (sizeof(keymap) / sizeof(keymap[0]))

	// Previous frame's button bitmask. Used to compute press/release deltas.
	// On each tic, we XOR current vs previous to find state changes.
	static int prev_buttons = 0;

	// Touch-screen gesture tracking, shared between the gameplay and
	// weapon-menu input paths so a tap can both open and select from the
	// menu.
	static int touch_active = 0;
	static int touch_last_x = 0;
	static int touch_down_x = 0;
	static int touch_down_y = 0;
	static boolean touch_dragged = false;

// True while the player holds L or R, so we only switch once per press
// (the engine's weapon cycle is edge-triggered anyway, but this keeps the
// auto-repeat of the hardware from spamming switches every frame).
static boolean weapon_switch_held = false;

// Build the owned-weapon mask and current weapon slot from the live
// player structure. Returns the current weapon slot (or -1 before play).
static int read_weapon_state(boolean owned[NDS_NUM_WEAPONS])
{
	for (int i = 0; i < NDS_NUM_WEAPONS; i++)
		owned[i] = false;

	if (gamestate != GS_LEVEL || !playeringame[consoleplayer])
		return -1;

	for (int i = 0; i < NUMWEAPONS && i < NDS_NUM_WEAPONS; i++)
		owned[i] = players[consoleplayer].weaponowned[i] != 0;

	int cur = (int)players[consoleplayer].readyweapon;
	if (cur < 0 || cur >= NDS_NUM_WEAPONS)
		cur = 0;
	return cur;
}

// Post a weapon-selection event. The engine maps digit keys '1'..'9' to
// weapon slots 0..8 (see weapon_keys[] in g_game.c), so sending the
// matching digit selects that weapon during the next ticcmd build.
//
// IMPORTANT: the keyup must NOT be posted in the same tic as the keydown.
// G_BuildTicCmd() reads gamekeydown[key] to decide the weapon; if we post
// keydown then keyup immediately, gamekeydown is cleared again before the
// ticcmd is built and the weapon never changes. So we post the keydown now
// and remember to release the key on the following I_StartTic.
static int pending_weapon_keyup = -1;

static void select_weapon(int slot)
{
	event_t ev;
	ev.type = ev_keydown;
	ev.data1 = '1' + slot;
	ev.data2 = ev.data3 = 0;
	D_PostEvent(&ev);
	pending_weapon_keyup = '1' + slot;
}

static void release_pending_weapon_key(void)
{
	if (pending_weapon_keyup < 0)
		return;
	event_t ev;
	ev.type = ev_keyup;
	ev.data1 = pending_weapon_keyup;
	ev.data2 = ev.data3 = 0;
	D_PostEvent(&ev);
	pending_weapon_keyup = -1;
}

// Find the next/previous owned weapon relative to the current ready weapon.
// direction is +1 for "next" (R) or -1 for "previous" (L). Returns the slot
// to select, or -1 if the player owns no weapons at all.
static int next_owned_weapon(int direction, const boolean owned[NDS_NUM_WEAPONS])
{
	int cur = read_weapon_state((boolean[NDS_NUM_WEAPONS]){0});
	if (cur < 0)
		cur = 0;

	int count = 0;
	for (int i = 0; i < NDS_NUM_WEAPONS; i++)
		if (owned[i])
			count++;
	if (count == 0)
		return -1;

	for (int step = 1; step <= NDS_NUM_WEAPONS; step++)
	{
		int cand = (cur + direction * step + NDS_NUM_WEAPONS * step)
		           % NDS_NUM_WEAPONS;
		if (owned[cand])
			return cand;
	}
	return -1;
}

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
	// Release any weapon digit key we posted on the previous tic so the
	// keydown is seen by exactly one ticcmd build (see select_weapon).
	release_pending_weapon_key();

	scanKeys();
	int held = keysHeld();
	int pressed = held & ~prev_buttons;
	int released = prev_buttons & ~held;
	prev_buttons = held;

	event_t ev;

	// Keep the bottom-screen HUD's automap indicator in sync with Doom's
	// real automap state. SELECT is mapped to KEY_TAB (see the keymap
	// table), so Doom itself toggles the automap overlay via AM_Responder;
	// we only mirror that state here for the HUD, which avoids the input
	// lockups a custom parallel toggle would cause.
	NDS_Panel_SetAutomap(automapactive);

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

	// A button only fires (KEY_RCTRL, from the keymap table above).
	// It no longer sends KEY_ENTER, so it does not refresh the status
	// message or phantom-confirm menus while shooting.

	// L / R switch to the previous / next owned weapon. Switching is
	// edge-triggered: we only act on the initial press, not while the
	// shoulder button is held, so a single tap = one switch.
	if ((pressed & (KEY_L | KEY_R)) && !weapon_switch_held)
	{
		boolean owned[NDS_NUM_WEAPONS];
		read_weapon_state(owned);
		int dir = (pressed & KEY_R) ? 1 : -1;
		int slot = next_owned_weapon(dir, owned);
		if (slot >= 0)
			select_weapon(slot);
		weapon_switch_held = true;
	}
	if (!(held & (KEY_L | KEY_R)))
		weapon_switch_held = false;

	// -------------------------------------------------------------------
	// Touch screen handling
	//
	// Two distinct gestures are recognised on the bottom (touch) screen:
	//
	//   * Drag  -- the stylus moves while held. The horizontal pixel
	//     delta is posted as a mouse event so the player can turn left
	//     and right. This is the primary look control.
	//
	//   * Tap   -- the stylus is pressed and released with negligible
	//     movement. A tap opens the weapon menu (mirroring SELECT) so
	//     weapons can be chosen directly by touching the desired entry.
	//     If the menu is already open, a tap selects the weapon whose
	//     row was touched.
	//
	// Only horizontal (X axis) movement is used for turning because
	// vanilla Doom has no vertical mouselook.
	// -------------------------------------------------------------------

	if (held & KEY_TOUCH)
	{
		touchPosition touch;
		touchRead(&touch);

		if (!touch_active)
		{
			// New touch begins: record the start point and reset the
			// drag flag. No mouse event yet (no previous position).
			touch_down_x = touch.px;
			touch_down_y = touch.py;
			touch_dragged = false;
			touch_last_x = touch.px;
			touch_active = 1;
		}
		else
		{
			int dx = touch.px - touch_last_x;
			int dy = touch.py - touch_down_y;

			// Treat movement beyond a few pixels as a drag (turn).
			if (abs(dx) > 2 || abs(dy) > 2)
				touch_dragged = true;

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
	}
	else
	{
		if (touch_active)
		{
			// Stylus released. A tap (no drag) does nothing special now
			// that weapon selection is on L/R; dragging is the turn
			// gesture and is handled above while the stylus is held.
		}
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
