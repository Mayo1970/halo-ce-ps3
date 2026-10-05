/* Pad polling as in ioQuake3-PS3 ps3_input.c and xashPS3 in_ps3.c. */

#include "ps3_input.h"
#include "ps3_system.h"

#include <string.h>

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
	initialized = 1;
}

void ps3_input_shutdown(void)
{
	if (initialized)
		ioPadEnd();
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
