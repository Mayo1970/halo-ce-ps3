/* Pad and USB keyboard polling as in ioQuake3-PS3 ps3_input.c and xashPS3 in_ps3.c. */

#include "ps3_input.h"
#include "ps3_system.h"

#include <string.h>

#include <io/kb.h>
#include <io/pad.h>

/* the length of padData in buttons: sticks from 8, pressures from 20 */
#define PAD_LENGTH_STICKS 8
#define PAD_LENGTH_PRESSURES 20

static struct ps3_pad pads[PS3_PAD_PORTS];
static padData pad_data[PS3_PAD_PORTS];
static int initialized;

static void decode(struct ps3_pad *pad, const padData *data)
{
	unsigned buttons = 0;

	if (data->BTN_SELECT) buttons |= PS3_PAD_SELECT;
	if (data->BTN_L3) buttons |= PS3_PAD_L3;
	if (data->BTN_R3) buttons |= PS3_PAD_R3;
	if (data->BTN_START) buttons |= PS3_PAD_START;
	if (data->BTN_UP) buttons |= PS3_PAD_UP;
	if (data->BTN_RIGHT) buttons |= PS3_PAD_RIGHT;
	if (data->BTN_DOWN) buttons |= PS3_PAD_DOWN;
	if (data->BTN_LEFT) buttons |= PS3_PAD_LEFT;
	if (data->BTN_L2) buttons |= PS3_PAD_L2;
	if (data->BTN_R2) buttons |= PS3_PAD_R2;
	if (data->BTN_L1) buttons |= PS3_PAD_L1;
	if (data->BTN_R1) buttons |= PS3_PAD_R1;
	if (data->BTN_TRIANGLE) buttons |= PS3_PAD_TRIANGLE;
	if (data->BTN_CIRCLE) buttons |= PS3_PAD_CIRCLE;
	if (data->BTN_CROSS) buttons |= PS3_PAD_CROSS;
	if (data->BTN_SQUARE) buttons |= PS3_PAD_SQUARE;
	pad->buttons = buttons;

	if (data->len >= PAD_LENGTH_STICKS)
	{
		pad->stick[PS3_PAD_STICK_LEFT_X] = (unsigned char)data->ANA_L_H;
		pad->stick[PS3_PAD_STICK_LEFT_Y] = (unsigned char)data->ANA_L_V;
		pad->stick[PS3_PAD_STICK_RIGHT_X] = (unsigned char)data->ANA_R_H;
		pad->stick[PS3_PAD_STICK_RIGHT_Y] = (unsigned char)data->ANA_R_V;
	}
	if (data->len >= PAD_LENGTH_PRESSURES)
	{
		pad->pressure[PS3_PAD_PRESSURE_RIGHT] = (unsigned char)data->PRE_RIGHT;
		pad->pressure[PS3_PAD_PRESSURE_LEFT] = (unsigned char)data->PRE_LEFT;
		pad->pressure[PS3_PAD_PRESSURE_UP] = (unsigned char)data->PRE_UP;
		pad->pressure[PS3_PAD_PRESSURE_DOWN] = (unsigned char)data->PRE_DOWN;
		pad->pressure[PS3_PAD_PRESSURE_TRIANGLE] = (unsigned char)data->PRE_TRIANGLE;
		pad->pressure[PS3_PAD_PRESSURE_CIRCLE] = (unsigned char)data->PRE_CIRCLE;
		pad->pressure[PS3_PAD_PRESSURE_CROSS] = (unsigned char)data->PRE_CROSS;
		pad->pressure[PS3_PAD_PRESSURE_SQUARE] = (unsigned char)data->PRE_SQUARE;
		pad->pressure[PS3_PAD_PRESSURE_L1] = (unsigned char)data->PRE_L1;
		pad->pressure[PS3_PAD_PRESSURE_R1] = (unsigned char)data->PRE_R1;
		pad->pressure[PS3_PAD_PRESSURE_L2] = (unsigned char)data->PRE_L2;
		pad->pressure[PS3_PAD_PRESSURE_R2] = (unsigned char)data->PRE_R2;
	}
}

void ps3_input_init(void)
{
	int port;

	ioPadInit(MAX_PORT_NUM);
	/* the pressures (L2 and R2 too) read 0 unless pressure mode is on */
	for (port = 0; port < PS3_PAD_PORTS; port++)
		ioPadSetPressMode(port, PAD_PRESS_MODE_ON);
	memset(pads, 0, sizeof(pads));
	memset(pad_data, 0, sizeof(pad_data));
	ioKbInit(MAX_KB_PORT_NUM);
	initialized = 1;
}

void ps3_input_shutdown(void)
{
	if (initialized)
	{
		ioPadEnd();
		ioKbEnd();
	}
	initialized = 0;
}

void ps3_input_poll(void)
{
	static padInfo info;
	int port;

	if (!initialized || ioPadGetInfo(&info) != 0)
		return;
	/* every port, every frame: after a Bluetooth sleep a pad can come back on another port */
	for (port = 0; port < PS3_PAD_PORTS; port++)
	{
		struct ps3_pad *pad = &pads[port];
		int connected = info.status[port] != 0;

		if (connected != pad->connected)
		{
			memset(pad, 0, sizeof(*pad));
			memset(pad->stick, 128, sizeof(pad->stick));
			pad->connected = connected;
			if (connected)
				ioPadSetPressMode(port, PAD_PRESS_MODE_ON);
			ps3_log_printf("[pad] port %d %s\n", port, connected ? "connected" : "disconnected");
		}
		if (!connected)
			continue;
		/* len 0 means no new data: keep the last state (a reset froze the ioQuake3-PS3 menu cursor) */
		if (ioPadGetData(port, &pad_data[port]) != 0 || pad_data[port].len == 0)
			continue;
		decode(pad, &pad_data[port]);
	}
}

const struct ps3_pad *ps3_input_pad(int port)
{
	return &pads[port];
}

/* the motors run until the next call (ioQuake3-PS3 PS3_SetRumble) */
void ps3_input_rumble(int port, int large, int small)
{
	padActParam act;

	if (!initialized || port < 0 || port >= PS3_PAD_PORTS)
		return;
	memset(&act, 0, sizeof(act));
	act.small_motor = small ? 1 : 0;
	act.large_motor = (u8)(large < 0 ? 0 : large > 255 ? 255 : large);
	ioPadSetActDirect(port, &act);
}

/* ---------- the USB keyboard (ioQuake3-PS3's packet reads) */

#define KEYSTROKE_QUEUE_SIZE 64
/* nb_keycode 0 is both "no keys" and "no new packet": a key no packet has named for this long is let go */
#define KEY_RELEASE_TIMEOUT 2.0
#define RAW_CODES 0x64

/* the Windows virtual key of each USB HID usage, as sdl_platform.c gives the game the Xbox debug keyboard's */
static const unsigned char raw_virtual_keys[RAW_CODES] =
{
	[0x04] = 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
	'U', 'V', 'W', 'X', 'Y', 'Z',
	[0x1E] = '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
	[0x28] = 0x0D, 0x1B, 0x08, 0x09, 0x20, 0xBD, 0xBB, 0xDB, 0xDD, 0xDC,
	[0x33] = 0xBA, 0xDE, 0xC0, 0xBC, 0xBE, 0xBF, 0x14,
	[0x3A] = 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B,
	[0x46] = 0x2C, 0x91, 0x13, 0x2D, 0x24, 0x21, 0x2E, 0x23, 0x22, 0x27, 0x25, 0x28, 0x26,
	[0x53] = 0x90, 0x6F, 0x6A, 0x6D, 0x6B, 0x0D, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x60, 0x6E,
};

/* the US layout's characters of 0x2D to 0x38 (0 for none), plain and with shift */
static const char punctuation[2][12] =
{
	{ '-', '=', '[', ']', '\\', 0, ';', '\'', '`', ',', '.', '/' },
	{ '_', '+', '{', '}', '|', 0, ':', '"', '~', '<', '>', '?' },
};

/* the modifier keys' bits (KbMkey) and virtual keys: left and right control, shift and alt */
static const struct
{
	unsigned bit;
	unsigned char virtual_key;
} modifier_keys[] =
{
	{ 0x01, 0xA2 }, { 0x02, 0xA0 }, { 0x04, 0xA4 }, { 0x10, 0xA3 }, { 0x20, 0xA1 }, { 0x40, 0xA5 },
};

static struct
{
	int port;
	unsigned char held[RAW_CODES];
	double seen[RAW_CODES];
	unsigned modifiers;
	unsigned leds;
	struct ps3_keystroke queue[KEYSTROKE_QUEUE_SIZE];
	int head, count;
} keyboard = { -1 };

static char raw_ascii(int raw, unsigned modifiers, unsigned leds)
{
	int shift = (modifiers & 0x22) != 0;
	static const char shifted_digits[] = "!@#$%^&*()";

	if (raw >= 0x04 && raw <= 0x1D)
		return (char)((shift ^ ((leds & 0x02) != 0) ? 'A' : 'a') + raw - 0x04);
	if (raw >= 0x1E && raw <= 0x27)
		return shift ? shifted_digits[raw - 0x1E] : (char)(raw == 0x27 ? '0' : '1' + raw - 0x1E);
	if (raw >= 0x2D && raw <= 0x38)
		return punctuation[shift][raw - 0x2D];
	if (raw >= 0x59 && raw <= 0x62)
		return (char)(raw == 0x62 ? '0' : '1' + raw - 0x59);
	switch (raw)
	{
	case 0x28: case 0x58: return '\r';
	case 0x29: return 0x1B;
	case 0x2A: return '\b';
	case 0x2B: return '\t';
	case 0x2C: return ' ';
	case 0x54: return '/';
	case 0x55: return '*';
	case 0x56: return '-';
	case 0x57: return '+';
	case 0x63: return '.';
	}
	return 0;
}

/* a key's change queued (the oldest dropped when full), with the modifiers' and locks' flags */
static void queue_key(unsigned char virtual_key, char ascii, int up)
{
	struct ps3_keystroke *keystroke;
	unsigned char flags = 0;

	if (!virtual_key)
		return;
	if (keyboard.count == KEYSTROKE_QUEUE_SIZE)
	{
		keyboard.head = (keyboard.head + 1) % KEYSTROKE_QUEUE_SIZE;
		keyboard.count--;
	}
	if (keyboard.modifiers & 0x11) flags |= 0x01;
	if (keyboard.modifiers & 0x22) flags |= 0x02;
	if (keyboard.modifiers & 0x44) flags |= 0x04;
	if (keyboard.leds & 0x02) flags |= 0x08;
	if (keyboard.leds & 0x01) flags |= 0x10;
	if (up) flags |= 0x40;
	keystroke = &keyboard.queue[(keyboard.head + keyboard.count) % KEYSTROKE_QUEUE_SIZE];
	keystroke->virtual_key = virtual_key;
	keystroke->ascii = up ? 0 : ascii;
	keystroke->flags = flags;
	keyboard.count++;
}

/* every key let go (the keyboard gone, or another taken) */
static void release_all(void)
{
	int index;

	for (index = 0; index < RAW_CODES; index++)
	{
		if (keyboard.held[index])
			queue_key(raw_virtual_keys[index], 0, 1);
	}
	for (index = 0; index < (int)(sizeof(modifier_keys) / sizeof(*modifier_keys)); index++)
	{
		if (keyboard.modifiers & modifier_keys[index].bit)
			queue_key(modifier_keys[index].virtual_key, 0, 1);
	}
	memset(keyboard.held, 0, sizeof(keyboard.held));
	keyboard.modifiers = 0;
}

void ps3_input_keyboard_poll(void)
{
	static KbInfo info;
	static KbData data;
	unsigned char held[RAW_CODES];
	double now;
	int port = -1, raw, index;

	if (!initialized || ioKbGetInfo(&info) != 0)
		return;
	for (index = 0; index < MAX_KB_PORT_NUM && port < 0; index++)
	{
		if (info.status[index])
			port = index;
	}
	if (port != keyboard.port)
	{
		release_all();
		keyboard.port = port;
		/* the code type before the read mode: the other order kills all input (ioQuake3-PS3) */
		if (port >= 0)
		{
			ioKbSetCodeType(port, KB_CODETYPE_RAW);
			ioKbSetReadMode(port, KB_RMODE_PACKET);
			ioKbClearBuf(port);
		}
		ps3_log_printf("[kb] %s %d\n", port >= 0 ? "keyboard on port" : "no keyboard", port);
	}
	if (port < 0 || ioKbRead(port, &data) != 0)
		return;
	now = ps3_time_seconds();
	memcpy(held, keyboard.held, sizeof(held));
	if (data.nb_keycode > 0)
	{
		/* a packet names every key held: the others are let go (KB_RAWKEY_NO_EVENT, 0, names none) */
		memset(held, 0, sizeof(held));
		for (index = 0; index < data.nb_keycode && index < MAX_KEYCODES; index++)
		{
			raw = data.keycode[index] & ~(KB_RAWDAT | KB_KEYPAD);
			if (raw > 0 && raw < RAW_CODES)
			{
				held[raw] = 1;
				keyboard.seen[raw] = now;
			}
		}
	}
	else
	{
		for (raw = 0; raw < RAW_CODES; raw++)
		{
			if (held[raw] && now - keyboard.seen[raw] > KEY_RELEASE_TIMEOUT)
				held[raw] = 0;
		}
	}
	keyboard.leds = data.led._KbLedU.leds;
	for (index = 0; index < (int)(sizeof(modifier_keys) / sizeof(*modifier_keys)); index++)
	{
		unsigned bit = modifier_keys[index].bit;

		if ((data.mkey._KbMkeyU.mkeys ^ keyboard.modifiers) & bit)
		{
			keyboard.modifiers ^= bit;
			queue_key(modifier_keys[index].virtual_key, 0, !(keyboard.modifiers & bit));
		}
	}
	for (raw = 0; raw < RAW_CODES; raw++)
	{
		if (held[raw] != keyboard.held[raw])
		{
			keyboard.held[raw] = held[raw];
			queue_key(raw_virtual_keys[raw], raw_ascii(raw, keyboard.modifiers, keyboard.leds), !held[raw]);
		}
	}
}

int ps3_input_keystroke(struct ps3_keystroke *keystroke)
{
	if (!keyboard.count)
		return 0;
	*keystroke = keyboard.queue[keyboard.head];
	keyboard.head = (keyboard.head + 1) % KEYSTROKE_QUEUE_SIZE;
	keyboard.count--;
	return 1;
}

int ps3_input_key_held(unsigned char virtual_key)
{
	int raw;

	for (raw = 0; raw < RAW_CODES; raw++)
	{
		if (keyboard.held[raw] && raw_virtual_keys[raw] == virtual_key)
			return 1;
	}
	return 0;
}
