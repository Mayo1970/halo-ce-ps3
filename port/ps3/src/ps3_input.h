/* DualShock 3 and Sixaxis pads on all 7 ports, polled once per frame. */

#ifndef __HALO_PS3_INPUT_H
#define __HALO_PS3_INPUT_H

#define PS3_PAD_PORTS 7

/* the bits of the pad's two digital button words */
enum
{
	PS3_PAD_SELECT = 1 << 0,
	PS3_PAD_L3 = 1 << 1,
	PS3_PAD_R3 = 1 << 2,
	PS3_PAD_START = 1 << 3,
	PS3_PAD_UP = 1 << 4,
	PS3_PAD_RIGHT = 1 << 5,
	PS3_PAD_DOWN = 1 << 6,
	PS3_PAD_LEFT = 1 << 7,
	PS3_PAD_L2 = 1 << 8,
	PS3_PAD_R2 = 1 << 9,
	PS3_PAD_L1 = 1 << 10,
	PS3_PAD_R1 = 1 << 11,
	PS3_PAD_TRIANGLE = 1 << 12,
	PS3_PAD_CIRCLE = 1 << 13,
	PS3_PAD_CROSS = 1 << 14,
	PS3_PAD_SQUARE = 1 << 15,
	PS3_PAD_BUTTON_COUNT = 16
};

enum
{
	PS3_PAD_STICK_LEFT_X,
	PS3_PAD_STICK_LEFT_Y,
	PS3_PAD_STICK_RIGHT_X,
	PS3_PAD_STICK_RIGHT_Y,
	PS3_PAD_STICK_COUNT
};

/* the pressure of the analog buttons, in the pad's order */
enum
{
	PS3_PAD_PRESSURE_RIGHT,
	PS3_PAD_PRESSURE_LEFT,
	PS3_PAD_PRESSURE_UP,
	PS3_PAD_PRESSURE_DOWN,
	PS3_PAD_PRESSURE_TRIANGLE,
	PS3_PAD_PRESSURE_CIRCLE,
	PS3_PAD_PRESSURE_CROSS,
	PS3_PAD_PRESSURE_SQUARE,
	PS3_PAD_PRESSURE_L1,
	PS3_PAD_PRESSURE_R1,
	PS3_PAD_PRESSURE_L2,
	PS3_PAD_PRESSURE_R2,
	PS3_PAD_PRESSURE_COUNT
};

struct ps3_pad
{
	int connected;
	unsigned buttons;
	/* 0 to 255, about 128 at the center; y is 0 at the top */
	unsigned char stick[PS3_PAD_STICK_COUNT];
	unsigned char pressure[PS3_PAD_PRESSURE_COUNT];
};

void ps3_input_init(void);
void ps3_input_shutdown(void);
void ps3_input_poll(void);
const struct ps3_pad *ps3_input_pad(int port);
/* large 0 to 255; the DS3's small motor is only on or off */
void ps3_input_rumble(int port, int large, int small);

#endif
