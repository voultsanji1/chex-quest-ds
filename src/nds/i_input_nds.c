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
//   Y -> quick weapon cycle (next owned weapon)
//     Doom uses number keys 1-9 for weapon selection. With limited
//     buttons there is no room for nine direct bindings, so Y advances
//     to the next owned weapon. For full bidirectional selection the
//     player opens the bottom-screen weapon menu with SELECT and taps
//     (or navigates with D-pad + A) the desired weapon.
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
	{ KEY_A,      KEY_RCTRL },     // Fire
	{ KEY_B,      ' ' },           // Use/Open (space)
	{ KEY_X,      KEY_RSHIFT },    // Run
	{ KEY_L,      ',' },           // Strafe left
	{ KEY_R,      '.' },           // Strafe right
	{ KEY_START,  KEY_ESCAPE },    // Menu
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

// Weapon menu state. When active, the bottom screen shows the list of
// weapons and game input (movement, fire, etc.) is suspended so the
// D-pad/A can drive menu navigation instead.
static boolean weapon_menu_active = false;
static int weapon_menu_sel = 0;

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

// Advance weapon_menu_sel to the next owned weapon (direction +1/-1),
// skipping slots the player does not have. Returns the new selection.
static int cycle_weapon_sel(int direction, const boolean owned[NDS_NUM_WEAPONS])
{
	int count = 0;
	for (int i = 0; i < NDS_NUM_WEAPONS; i++)
		if (owned[i])
			count++;
	if (count == 0)
		return weapon_menu_sel;

	for (int step = 1; step <= NDS_NUM_WEAPONS; step++)
	{
		int next = (weapon_menu_sel + direction * step + NDS_NUM_WEAPONS * step)
		           % NDS_NUM_WEAPONS;
		if (owned[next])
			return next;
	}
	return weapon_menu_sel;
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

	// SELECT toggles the bottom-screen weapon menu. While the menu is
	// open we handle navigation locally and suppress normal gameplay
	// button mapping so the D-pad drives the menu instead of movement.
	if (pressed & KEY_SELECT)
	{
		weapon_menu_active = !weapon_menu_active;
		NDS_Panel_SetWeaponMenu(weapon_menu_active);
		if (!weapon_menu_active)
			NDS_Panel_ForceGameplayRedraw();
		else
		{
			// Release any gameplay keys that were held when the menu
			// opened so the player stops moving/firing while choosing.
			event_t ev;
			for (unsigned int i = 0; i < NUM_KEYMAPS; i++)
			{
				if (held & keymap[i].nds_key)
				{
					ev.type = ev_keyup;
					ev.data1 = keymap[i].doom_key;
					ev.data2 = ev.data3 = 0;
					D_PostEvent(&ev);
				}
			}
			if (held & KEY_A)
			{
				ev.type = ev_keyup;
				ev.data1 = KEY_ENTER;
				ev.data2 = ev.data3 = 0;
				D_PostEvent(&ev);
			}
		}
	}

	if (weapon_menu_active)
	{
		boolean owned[NDS_NUM_WEAPONS];
		int current = read_weapon_state(owned);

		// Keep the highlight on a valid owned weapon.
		if (!owned[weapon_menu_sel])
			weapon_menu_sel = cycle_weapon_sel(1, owned);

		if (pressed & KEY_UP)
			weapon_menu_sel = cycle_weapon_sel(-1, owned);
		if (pressed & KEY_DOWN)
			weapon_menu_sel = cycle_weapon_sel(1, owned);

		// A confirms the selection and closes the menu.
		if (pressed & KEY_A)
		{
			if (owned[weapon_menu_sel])
				select_weapon(weapon_menu_sel);
			weapon_menu_active = false;
			NDS_Panel_SetWeaponMenu(false);
			NDS_Panel_ForceGameplayRedraw();
		}

		// While the menu is open, a tap on a weapon row selects it.
		// Dragging is ignored (no turning while choosing a weapon).
		if (held & KEY_TOUCH)
		{
			touchPosition touch;
			touchRead(&touch);
			if (!touch_active)
			{
				touch_down_x = touch.px;
				touch_down_y = touch.py;
				touch_dragged = false;
				touch_last_x = touch.px;
				touch_active = 1;
			}
		}
		else if (touch_active)
		{
			touch_active = 0;
			if (!touch_dragged)
			{
				int row = (touch_down_y * 24) / 256 - 4;
				if (row >= 0 && row < NDS_NUM_WEAPONS && owned[row])
				{
					select_weapon(row);
					weapon_menu_active = false;
					NDS_Panel_SetWeaponMenu(false);
					NDS_Panel_ForceGameplayRedraw();
				}
			}
		}

		NDS_Panel_DrawWeaponMenu(weapon_menu_sel, owned, current);

		// The weapon menu consumes all other input this frame; the
		// touch-turn handler below is skipped while the menu is open.
		return;
	}

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

	// Y performs a quick "next owned weapon" cycle. Full bidirectional
	// selection is available through the SELECT weapon menu.
	if (pressed & KEY_Y)
	{
		boolean owned[NDS_NUM_WEAPONS];
		read_weapon_state(owned);
		int next = cycle_weapon_sel(1, owned);
		if (owned[next])
			select_weapon(next);
	}

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
			// Stylus released: a tap (no drag) toggles/uses the menu.
			if (!touch_dragged)
			{
				if (!weapon_menu_active)
				{
					weapon_menu_active = true;
				}
				else
				{
					// Map the tap's vertical position to a weapon row.
					// The menu draws weapons starting at console row 5,
					// and the 256px-tall touch screen maps to 24 rows.
					int row = (touch_down_y * 24) / 256 - 4;
					if (row >= 0 && row < NDS_NUM_WEAPONS)
					{
						boolean owned[NDS_NUM_WEAPONS];
						read_weapon_state(owned);
					if (owned[row])
					{
						select_weapon(row);
						weapon_menu_active = false;
						NDS_Panel_SetWeaponMenu(false);
						NDS_Panel_ForceGameplayRedraw();
					}
					}
				}
			}
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
