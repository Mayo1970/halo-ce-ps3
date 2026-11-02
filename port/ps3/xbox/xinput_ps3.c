/* XInput on the PS3 pads (src/ps3_input.c). A pad takes the lowest free Xbox port and keeps it until it
disconnects; all pad calls run on the game's thread, which polls once a frame in XGetDeviceChanges. */

#include "ps3_input.h"
#include "ps3_osk.h"
#include "halo_keyboard.h"

#include <stdio.h>
#include <string.h>

#define PORT_COUNT 4
/* a DS3's small motor only turns on or off: on from half of the right motor's speed */
#define SMALL_MOTOR_THRESHOLD 0x8000
/* a chat line from the system keyboard: at most OpenCE's 63 characters, a few keys a tick to the game's 64
buffered keystrokes, and chat's line given this long to open */
#define OSK_CHAT_CHARACTERS 63
#define OSK_KEYS_PER_TICK 16
#define OSK_CHAT_OPEN_TIMEOUT 1000

/* (the game's game.h: a network game under way, client or host) */
unsigned char game_in_progress(void);
short game_connection(void);
#define GAME_CONNECTION_NETWORK_CLIENT 1
#define GAME_CONNECTION_NETWORK_SERVER 2

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
	/* the PS3 pad's buttons at the last read, and those hidden from the game until let go (the chat combo's) */
	unsigned pad_buttons;
	unsigned hidden;
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

static void pad_state(const struct ps3_pad *pad, unsigned hidden, XINPUT_GAMEPAD *gamepad)
{
	unsigned buttons = pad->buttons & ~hidden;
	int index;

	for (index = 0; index < (int)(sizeof(digital_buttons) / sizeof(digital_buttons[0])); index++)
	{
		if (buttons & digital_buttons[index].pad)
			gamepad->wButtons |= digital_buttons[index].mask;
	}
	for (index = 0; index < (int)(sizeof(analog_buttons) / sizeof(analog_buttons[0])); index++)
	{
		if (buttons & analog_buttons[index].pad)
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
	/* (the keyboard is never reported: the game reads keystrokes without one open, input_xbox.c) */
	else if (device_type == XDEVICE_TYPE_DEBUG_KEYBOARD && initialized)
	{
		ps3_input_keyboard_poll();
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
	{
		const struct ps3_pad *pad = ps3_input_pad(controller->pad);
		unsigned pressed = pad->buttons & ~controller->pad_buttons;
		short connection;

		controller->pad_buttons = pad->buttons;
		controller->hidden &= pad->buttons;
		/* SELECT and CROSS in a network game: chat's line on the system keyboard, the two kept from the game */
		if ((pad->buttons & PS3_PAD_SELECT) && (pressed & PS3_PAD_CROSS) && !ps3_osk_active() && game_in_progress())
		{
			connection = game_connection();
			if (connection == GAME_CONNECTION_NETWORK_CLIENT || connection == GAME_CONNECTION_NETWORK_SERVER)
			{
				ps3_osk_open("Chat", OSK_CHAT_CHARACTERS);
				controller->hidden = pad->buttons;
			}
		}
		/* (while the keyboard is up, nothing; the buttons held as it closes are kept from the game until let go) */
		if (ps3_osk_active())
			controller->hidden = pad->buttons;
		else
			pad_state(pad, controller->hidden, gamepad);
	}
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

/* text chat's line or log is up (chat.c), and escape pressed meanwhile */
static BOOL chat_typing, chat_escape_pressed;

/* the system keyboard's chat line: chat's line opened as T opens it, then typed key by key and said with Enter */
static struct
{
	BOOL pending;
	char text[OSK_CHAT_CHARACTERS + 1];
	/* the next key event: two a character (down, up), then Enter's two */
	int event;
	DWORD asked;
	int keys_this_tick;
} osk_line;

/* a character's virtual key on the US layout, and whether it takes shift; 0 for none */
static unsigned char character_key(char character, BOOL *shift)
{
	static const char plain[] = "-=[]\\;'`,./", shifted[] = "_+{}|:\"~<>?", digits[] = ")!@#$%^&*(";
	static const unsigned char punctuation[] = { 0xBD, 0xBB, 0xDB, 0xDD, 0xDC, 0xBA, 0xDE, 0xC0, 0xBC, 0xBE, 0xBF };
	const char *found;

	*shift = FALSE;
	if (character >= 'a' && character <= 'z')
		return (unsigned char)(character - 'a' + 'A');
	if ((character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') || character == ' ')
	{
		*shift = character >= 'A' && character <= 'Z';
		return (unsigned char)character;
	}
	if (character && (found = strchr(digits, character)) != NULL)
	{
		*shift = TRUE;
		return (unsigned char)('0' + (found - digits));
	}
	if (character && (found = strchr(plain, character)) != NULL)
		return punctuation[found - plain];
	if (character && (found = strchr(shifted, character)) != NULL)
	{
		*shift = TRUE;
		return punctuation[found - shifted];
	}
	return 0;
}

/* the line's next key, once chat's line is open; FALSE when this tick has had its keys or the line is said */
static BOOL osk_line_key(PXINPUT_DEBUG_KEYSTROKE keystroke)
{
	int length = (int)strlen(osk_line.text);
	BOOL shift = FALSE;
	int character = osk_line.event / 2;

	if (!osk_line.pending || !chat_typing || osk_line.keys_this_tick >= OSK_KEYS_PER_TICK)
		return FALSE;
	/* (a character the game has no key for is passed over) */
	while (character < length && !character_key(osk_line.text[character], &shift))
		osk_line.event = 2 * ++character;
	keystroke->VirtualKey = character < length ? character_key(osk_line.text[character], &shift) : 0x0D;
	keystroke->Flags = shift ? XINPUT_DEBUG_KEYSTROKE_FLAG_SHIFT : 0;
	if (osk_line.event & 1)
		keystroke->Flags |= XINPUT_DEBUG_KEYSTROKE_FLAG_KEYUP;
	else
		keystroke->Ascii = character < length ? osk_line.text[character] : '\r';
	osk_line.event++;
	osk_line.keys_this_tick++;
	/* (Enter's release given: the line is done before chat closes it) */
	if (osk_line.event >= 2 * (length + 1))
		osk_line.pending = FALSE;
	return TRUE;
}

/* the USB keyboard's: key ups always (no key left latched), key downs only to text chat, as xinput_sdl.c */
DWORD WINAPI XInputDebugGetKeystroke(PXINPUT_DEBUG_KEYSTROKE keystroke)
{
	struct ps3_keystroke next;

	memset(keystroke, 0, sizeof(*keystroke));
	if (osk_line_key(keystroke))
		return ERROR_SUCCESS;
	osk_line.keys_this_tick = 0;
	while (ps3_input_keystroke(&next))
	{
		BOOL key_up = (next.flags & XINPUT_DEBUG_KEYSTROKE_FLAG_KEYUP) != 0;

		if (chat_typing && !key_up && next.virtual_key == 0x1B)
			chat_escape_pressed = TRUE;
		if (key_up || chat_typing)
		{
			keystroke->VirtualKey = next.virtual_key;
			keystroke->Ascii = next.ascii;
			keystroke->Flags = next.flags;
			return ERROR_SUCCESS;
		}
	}
	return ERROR_HANDLE_EOF;
}

void platform_chat_typing(int typing)
{
	chat_typing = typing != 0;
	chat_escape_pressed = FALSE;
}

int platform_chat_escape(void)
{
	BOOL pressed = chat_escape_pressed;

	chat_escape_pressed = FALSE;
	return pressed;
}

/* ---------- the desktop's keyboard and mouse (xinput_sdl.c): the keyboard's chat keys alone, T, Y and H as the
desktop's defaults, for the first player; none while chat types */

unsigned long halo_keyboard_actions(short controller_index)
{
	unsigned long actions = 0;

	if (controller_index != 0 || chat_typing)
		return 0;
	/* a system keyboard's line: chat's line asked open as T asks it, till it opens (a host without chat: given up) */
	if (!osk_line.pending && ps3_osk_take_text(osk_line.text, sizeof(osk_line.text)))
	{
		osk_line.pending = TRUE;
		osk_line.event = 0;
		osk_line.keys_this_tick = 0;
		osk_line.asked = GetTickCount();
	}
	if (osk_line.pending)
	{
		if (GetTickCount() - osk_line.asked < OSK_CHAT_OPEN_TIMEOUT)
			return 1UL << HALO_KEYBOARD_CHAT_ALL;
		osk_line.pending = FALSE;
	}
	if (ps3_input_key_held('T'))
		actions |= 1UL << HALO_KEYBOARD_CHAT_ALL;
	if (ps3_input_key_held('Y'))
		actions |= 1UL << HALO_KEYBOARD_CHAT_TEAM;
	if (ps3_input_key_held('H'))
		actions |= 1UL << HALO_KEYBOARD_CHAT_LOG;
	return actions;
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
