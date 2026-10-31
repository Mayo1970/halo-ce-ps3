/* The system's on-screen keyboard as ioQuake3-PS3's ps3_osk.c, but for its memory container: made at each opening
and given back at the close, so the game keeps its 4 MB while the keyboard is down. */

#include "ps3_osk.h"
#include "ps3_system.h"

#include <string.h>

#include <ppu-types.h>
#include <sys/memory.h>
#include <sysutil/osk.h>
#include <sysutil/sysutil.h>

/* the firmware's recommended container */
#define OSK_CONTAINER_SIZE (4 * 1024 * 1024)
#define OSK_TEXT 256

enum
{
	_osk_idle,
	_osk_opening,
	_osk_running,
	_osk_closing
};

/* the game's thread opens it; the sysutil callback's thread (the monitor's, or a frame's) moves it on */
static volatile int osk_state = _osk_idle;
static sys_mem_container_t osk_container;
static u16 osk_title[OSK_TEXT];
static u16 osk_text[OSK_TEXT];
static oskCallbackReturnParam osk_return;
static char osk_entered[OSK_TEXT];
static volatile int osk_entered_ready;

int ps3_osk_open(const char *title, int maximum_length)
{
	oskParam param;
	oskInputFieldInfo input;
	s32 result;
	int index;

	if (osk_state != _osk_idle)
		return 0;
	if (sysMemContainerCreate(&osk_container, OSK_CONTAINER_SIZE) != 0)
	{
		ps3_log_printf("[osk] no room for the keyboard's %d MB container\n", OSK_CONTAINER_SIZE >> 20);
		return 0;
	}
	if (maximum_length < 1)
		maximum_length = 1;
	if (maximum_length > OSK_TEXT - 1)
		maximum_length = OSK_TEXT - 1;
	for (index = 0; index < OSK_TEXT - 1 && title[index]; index++)
		osk_title[index] = (u16)(unsigned char)title[index];
	osk_title[index] = 0;
	memset(osk_text, 0, sizeof(osk_text));
	memset(&input, 0, sizeof(input));
	input.message = osk_title;
	input.startText = osk_text;
	input.maxLength = maximum_length;
	memset(&param, 0, sizeof(param));
	param.allowedPanels = OSK_PANEL_TYPE_ALPHABET | OSK_PANEL_TYPE_NUMERAL | OSK_PANEL_TYPE_URL;
	param.firstViewPanel = OSK_PANEL_TYPE_ALPHABET;
	param.prohibitFlags = OSK_PROHIBIT_RETURN;
	oskSetKeyLayoutOption(OSK_FULLKEY_PANEL);
	oskSetInitialKeyLayout(OSK_INITIAL_FULLKEY_PANEL);
	oskSetInitialInputDevice(OSK_DEVICE_PAD);
	oskDisableDimmer();
	/* (opening before the load: its LOADED event can come on another thread at once) */
	osk_state = _osk_opening;
	result = oskLoadAsync(osk_container, &param, &input);
	if (result != 0)
	{
		ps3_log_printf("[osk] oskLoadAsync=0x%08x\n", (unsigned)result);
		sysMemContainerDestroy(osk_container);
		osk_state = _osk_idle;
		return 0;
	}
	ps3_log_printf("[osk] open\n");
	return 1;
}

int ps3_osk_active(void)
{
	return osk_state != _osk_idle;
}

int ps3_osk_take_text(char *text, int size)
{
	if (!osk_entered_ready || size <= 0)
		return 0;
	__sync_synchronize();
	strncpy(text, osk_entered, (size_t)size - 1);
	text[size - 1] = 0;
	osk_entered_ready = 0;
	return 1;
}

void ps3_osk_event(uint64_t status)
{
	switch (status)
	{
	case SYSUTIL_OSK_LOADED:
		if (osk_state == _osk_opening)
			osk_state = _osk_running;
		break;
	case SYSUTIL_OSK_DONE:
	case SYSUTIL_OSK_INPUT_CANCELED:
		if (osk_state == _osk_running)
		{
			memset(&osk_return, 0, sizeof(osk_return));
			osk_return.str = osk_text;
			osk_return.len = OSK_TEXT - 1;
			osk_state = _osk_closing;
			oskUnloadAsync(&osk_return);
		}
		break;
	case SYSUTIL_OSK_UNLOADED:
		if (osk_state == _osk_closing)
		{
			int index, length = 0;

			/* (the game's fonts draw ASCII: the rest is left out) */
			for (index = 0; osk_return.res == OSK_OK && index < osk_return.len && index < OSK_TEXT - 1 &&
				osk_text[index]; index++)
			{
				if (osk_text[index] >= 32 && osk_text[index] <= 126)
					osk_entered[length++] = (char)osk_text[index];
			}
			osk_entered[length] = 0;
			if (length)
			{
				__sync_synchronize();
				osk_entered_ready = 1;
			}
			sysMemContainerDestroy(osk_container);
			osk_state = _osk_idle;
			ps3_log_printf("[osk] closed (%d characters)\n", length);
		}
		break;
	}
}
