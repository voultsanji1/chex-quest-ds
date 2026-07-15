// Game controller type stubs.
//
// NDS has no gamepad API. These types exist because Chocolate Doom's
// joystick code references SDL_GameController types. Actual input uses
// libnds keyscan in i_input_nds.c.

#ifndef SDL_GAMECONTROLLER_H_SHIM
#define SDL_GAMECONTROLLER_H_SHIM

// Button enum referenced by Chocolate Doom's joystick mapping tables.
// The values are never acted on because NDS input bypasses SDL entirely.
typedef enum
{
	SDL_CONTROLLER_BUTTON_A,
	SDL_CONTROLLER_BUTTON_B,
	SDL_CONTROLLER_BUTTON_X,
	SDL_CONTROLLER_BUTTON_Y,
	SDL_CONTROLLER_BUTTON_MAX
} SDL_GameControllerButton;

// Axis enum referenced by analog stick mapping code. NDS has no analog
// sticks, so these values are never read at runtime.
typedef enum
{
	SDL_CONTROLLER_AXIS_LEFTX,
	SDL_CONTROLLER_AXIS_LEFTY,
	SDL_CONTROLLER_AXIS_RIGHTX,
	SDL_CONTROLLER_AXIS_RIGHTY,
	SDL_CONTROLLER_AXIS_TRIGGERLEFT,
	SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
	SDL_CONTROLLER_AXIS_MAX
} SDL_GameControllerAxis;

// Opaque type used in function signatures. Aliased to int because the
// NDS platform layer never instantiates a real controller object.
typedef int SDL_GameControllerType;

#endif
