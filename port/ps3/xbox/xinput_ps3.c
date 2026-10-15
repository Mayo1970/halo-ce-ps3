/* XInput on the PS3 pads (src/ps3_input.c). A pad takes the lowest free Xbox port and keeps it until it
disconnects; all pad calls run on the game's thread, which polls once a frame in XGetDeviceChanges. */

#include "ps3_input.h"

#include <stdio.h>
#include <string.h>

#define PORT_COUNT 4
/* a DS3's small motor only turns on or off: on from half of the right motor's speed */
#define SMALL_MOTOR_THRESHOLD 0x8000

XPP_DEVICE_TYPE XDEVICE_TYPE_GAMEPAD_TABLE;
XPP_DEVICE_TYPE XDEVICE_TYPE_MEMORY_UNIT_TABLE;
XPP_DEVICE_TYPE XDEVICE_TYPE_DEBUG_KEYBOARD_TABLE;

struct controller
{
	BOOL open;
	/* the PS3 pad port in this Xbox port, or -1 */
	int pad;
	DWORD packet_number;
	XINPUT_GAMEPAD previous;
	/* XInputSetState's speeds (left << 16 | right), written by the game's input thread */
	volatile DWORD motors;
	/* the actuator values last sent to the pad */
	int large_motor;
	int small_motor;
};

static struct controller controllers[PORT_COUNT];
static BOOL initialized;
/* the ports that the game knows to have a gamepad */
static DWORD reported_gamepads;

static const struct
{
	unsigned pad;
	WORD mask;
} digital_buttons[] =
{
	{ PS3_PAD_UP, XINPUT_GAMEPAD_DPAD_UP },
	{ PS3_PAD_DOWN, XINPUT_GAMEPAD_DPAD_DOWN },
	{ PS3_PAD_LEFT, XINPUT_GAMEPAD_DPAD_LEFT },
	{ PS3_PAD_RIGHT, XINPUT_GAMEPAD_DPAD_RIGHT },
	{ PS3_PAD_START, XINPUT_GAMEPAD_START },
	{ PS3_PAD_SELECT, XINPUT_GAMEPAD_BACK },
	{ PS3_PAD_L3, XINPUT_GAMEPAD_LEFT_THUMB },
	{ PS3_PAD_R3, XINPUT_GAMEPAD_RIGHT_THUMB },
};

/* the Duke's analog buttons read 255 while held: the game only compares them with its thresholds.
The white and black buttons are on the shoulders, as in xinput_sdl.c. */
static const struct
{
	unsigned pad;
	int index;
} analog_buttons[] =
{
	{ PS3_PAD_CROSS, XINPUT_GAMEPAD_A },
	{ PS3_PAD_CIRCLE, XINPUT_GAMEPAD_B },
	{ PS3_PAD_SQUARE, XINPUT_GAMEPAD_X },
	{ PS3_PAD_TRIANGLE, XINPUT_GAMEPAD_Y },
	{ PS3_PAD_L1, XINPUT_GAMEPAD_WHITE },
	{ PS3_PAD_R1, XINPUT_GAMEPAD_BLACK },
	{ PS3_PAD_L2, XINPUT_GAMEPAD_LEFT_TRIGGER },
	{ PS3_PAD_R2, XINPUT_GAMEPAD_RIGHT_TRIGGER },
};

/* 0 to 255 with 128 at the center (y 0 at the top) to -32768 to 32767 (y up) */
static SHORT stick(unsigned char value, BOOL flip)
{
	int centered = (int)value - 128;
	int result = centered >= 0 ? centered * 32767 / 127 : centered * 256;

	return (SHORT)(flip ? -result - 1 : result);
}

static void pad_state(const struct ps3_pad *pad, XINPUT_GAMEPAD *gamepad)
{
	int index;

	for (index = 0; index < (int)(sizeof(digital_buttons) / sizeof(digital_buttons[0])); index++)
	{
		if (pad->buttons & digital_buttons[index].pad)
			gamepad->wButtons |= digital_buttons[index].mask;
	}
	for (index = 0; index < (int)(sizeof(analog_buttons) / sizeof(analog_buttons[0])); index++)
	{
		if (pad->buttons & analog_buttons[index].pad)
			gamepad->bAnalogButtons[analog_buttons[index].index] = 0xff;
	}
	gamepad->sThumbLX = stick(pad->stick[PS3_PAD_STICK_LEFT_X], FALSE);
	gamepad->sThumbLY = stick(pad->stick[PS3_PAD_STICK_LEFT_Y], TRUE);
	gamepad->sThumbRX = stick(pad->stick[PS3_PAD_STICK_RIGHT_X], FALSE);
	gamepad->sThumbRY = stick(pad->stick[PS3_PAD_STICK_RIGHT_Y], TRUE);
}

/* the buttons the game gets, at each change (not the sticks) */
static void log_buttons(int port, const XINPUT_GAMEPAD *gamepad)
{
	static const char *const analog_names[8] = { "A", "B", "X", "Y", "black", "white", "LT", "RT" };
	static const char *const digital_names[8] = { "up", "down", "left", "right", "start", "back", "LS", "RS" };
	char text[96];
	int index, length = 0;

	text[0] = '\0';
	for (index = 0; index < 8; index++)
	{
		if (gamepad->bAnalogButtons[index])
			length += snprintf(text + length, sizeof(text) - length, " %s", analog_names[index]);
	}
	for (index = 0; index < 8; index++)
	{
		if (gamepad->wButtons & (1 << index))
			length += snprintf(text + length, sizeof(text) - length, " %s", digital_names[index]);
	}
	platform_log("XInput: port %d:%s", port, length ? text : " none");
}

/* frees the ports of the pads that left, then gives free ports to new pads; returns the ports that lost
their pad (the game closes them, and opens them again if a new pad took them) */
static DWORD pads_assign(void)
{
	BOOL assigned[PS3_PAD_PORTS];
	DWORD lost = 0;
	int port, pad;

	memset(assigned, 0, sizeof(assigned));
	for (port = 0; port < PORT_COUNT; port++)
	{
		struct controller *controller = &controllers[port];

		if (controller->pad < 0)
			continue;
		if (ps3_input_pad(controller->pad)->connected)
		{
			assigned[controller->pad] = TRUE;
			continue;
		}
		platform_log("XInput: port %d lost PS3 pad %d", port, controller->pad);
		controller->pad = -1;
		lost |= 1u << port;
	}
	for (pad = 0; pad < PS3_PAD_PORTS; pad++)
	{
		if (assigned[pad] || !ps3_input_pad(pad)->connected)
			continue;
		for (port = 0; port < PORT_COUNT && controllers[port].pad >= 0; port++)
			;
		if (port == PORT_COUNT)
			break;
		controllers[port].pad = pad;
		controllers[port].large_motor = 0;
		controllers[port].small_motor = 0;
		platform_log("XInput: PS3 pad %d is port %d", pad, port);
	}
	return lost;
}

/* sends the game's motor speeds when they change; none while closed */
static void motors_update(void)
{
	int port;

	for (port = 0; port < PORT_COUNT; port++)
	{
		struct controller *controller = &controllers[port];
		DWORD motors = controller->open ? controller->motors : 0;
		int large = (int)(motors >> 24);
		int small = (motors & 0xffff) >= SMALL_MOTOR_THRESHOLD;

		if (controller->pad < 0 || (large == controller->large_motor && small == controller->small_motor))
			continue;
		ps3_input_rumble(controller->pad, large, small);
		controller->large_motor = large;
		controller->small_motor = small;
	}
}

static struct controller *controller_get(HANDLE device)
{
	int port;

	for (port = 0; port < PORT_COUNT; port++)
	{
		if (device == (HANDLE)&controllers[port] && controllers[port].open)
			return &controllers[port];
	}
	return NULL;
}

/* ---------- XAPI */

VOID WINAPI XInitDevices(DWORD preallocation_type_count, PXDEVICE_PREALLOC_TYPE preallocation_types)
{
	int port;

	(void)preallocation_type_count;
	(void)preallocation_types;
	if (!initialized)
		ps3_input_init();
	memset(controllers, 0, sizeof(controllers));
	for (port = 0; port < PORT_COUNT; port++)
		controllers[port].pad = -1;
	reported_gamepads = 0;
	initialized = TRUE;
	platform_log("XInput: the PS3 pads as %d gamepads", PORT_COUNT);
}

BOOL WINAPI XGetDeviceChanges(PXPP_DEVICE_TYPE device_type, PDWORD insertions, PDWORD removals)
{
	*insertions = 0;
	*removals = 0;
	if (device_type == XDEVICE_TYPE_GAMEPAD && initialized)
	{
		DWORD connected = 0, lost;
		int port;

		ps3_input_poll();
		lost = pads_assign();
		motors_update();
		for (port = 0; port < PORT_COUNT; port++)
		{
			if (controllers[port].pad >= 0)
				connected |= 1u << port;
		}
		*removals = reported_gamepads & (~connected | lost);
		*insertions = connected & (~reported_gamepads | lost);
		reported_gamepads = connected;
	}
	return *insertions || *removals;
}

HANDLE WINAPI XInputOpen(PXPP_DEVICE_TYPE device_type, DWORD port, DWORD slot,
	PXINPUT_POLLING_PARAMETERS polling_parameters)
{
	(void)slot;
	(void)polling_parameters;
	if (device_type == XDEVICE_TYPE_GAMEPAD && initialized && port < PORT_COUNT && controllers[port].pad >= 0)
	{
		struct controller *controller = &controllers[port];

		controller->open = TRUE;
		controller->packet_number = 0;
		memset(&controller->previous, 0, sizeof(controller->previous));
		controller->motors = 0;
		platform_log("XInput: port %d open (PS3 pad %d)", (int)port, controller->pad);
		return (HANDLE)controller;
	}
	SetLastError(ERROR_DEVICE_NOT_CONNECTED);
	return NULL;
}

VOID WINAPI XInputClose(HANDLE device)
{
	struct controller *controller = controller_get(device);

	if (!controller)
		return;
	controller->open = FALSE;
	controller->motors = 0;
	platform_log("XInput: port %d closed", (int)(controller - controllers));
}

DWORD WINAPI XInputGetState(HANDLE device, PXINPUT_STATE state)
{
	struct controller *controller = controller_get(device);
	XINPUT_GAMEPAD *gamepad = &state->Gamepad;

	memset(state, 0, sizeof(*state));
	if (!controller)
		return ERROR_DEVICE_NOT_CONNECTED;
	if (controller->pad >= 0)
		pad_state(ps3_input_pad(controller->pad), gamepad);
	if (memcmp(gamepad, &controller->previous, sizeof(*gamepad)))
	{
		if (gamepad->wButtons != controller->previous.wButtons ||
			memcmp(gamepad->bAnalogButtons, controller->previous.bAnalogButtons, sizeof(gamepad->bAnalogButtons)))
		{
			log_buttons((int)(controller - controllers), gamepad);
		}
		controller->packet_number++;
		controller->previous = *gamepad;
	}
	state->dwPacketNumber = controller->packet_number;
	return controller->pad >= 0 ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED;
}

/* called by the game's input thread at the vertical blank rate */
DWORD WINAPI XInputSetState(HANDLE device, PXINPUT_FEEDBACK feedback)
{
	struct controller *controller = controller_get(device);

	if (!feedback)
		return ERROR_INVALID_PARAMETER;
	feedback->Header.dwStatus = ERROR_SUCCESS;
	if (!controller)
		return ERROR_DEVICE_NOT_CONNECTED;
	controller->motors = (DWORD)feedback->Rumble.wLeftMotorSpeed << 16 | feedback->Rumble.wRightMotorSpeed;
	return ERROR_SUCCESS;
}

DWORD WINAPI XInputDebugInitKeyboardQueue(PXINPUT_DEBUG_KEYQUEUE_PARAMETERS parameters)
{
	(void)parameters;
	return ERROR_SUCCESS;
}

DWORD WINAPI XInputDebugGetKeystroke(PXINPUT_DEBUG_KEYSTROKE keystroke)
{
	memset(keystroke, 0, sizeof(*keystroke));
	return ERROR_HANDLE_EOF;
}

/* ---------- the desktop's keyboard and mouse (xinput_sdl.c): none on the PS3 */

unsigned long halo_keyboard_actions(short controller_index)
{
	(void)controller_index;
	return 0;
}

int halo_linux_mouse_look(short gamepad_index, float *yaw, float *pitch)
{
	(void)gamepad_index;
	*yaw = 0.0f;
	*pitch = 0.0f;
	return FALSE;
}

int halo_linux_mouse_aiming(short gamepad_index)
{
	(void)gamepad_index;
	return FALSE;
}

/* (no touch screen, no push-to-talk key) */
int halo_linux_touch_move(short controller_index, float *forward, float *strafe)
{
	(void)controller_index;
	(void)forward;
	(void)strafe;
	return FALSE;
}

int halo_linux_touch_look(short gamepad_index, float *yaw, float *pitch, float *gyro_yaw, float *gyro_pitch)
{
	(void)gamepad_index;
	(void)yaw;
	(void)pitch;
	(void)gyro_yaw;
	(void)gyro_pitch;
	return FALSE;
}

int halo_linux_touch_aiming(short gamepad_index)
{
	(void)gamepad_index;
	return FALSE;
}

int halo_push_to_talk_held(void)
{
	return FALSE;
}

void halo_input_name(int input, char *name, unsigned int size)
{
	(void)input;
	if (size)
		name[0] = '\0';
}

void platform_text_typing(int typing)
{
	(void)typing;
}

void platform_text_field(int typing)
{
	(void)typing;
}

void test_input_hold_action(int hold)
{
	(void)hold;
}
