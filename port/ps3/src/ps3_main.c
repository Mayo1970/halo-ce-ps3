/* PS3 entry point: the platform layer, then the game's main() (halo_xbox_main). Until G4 draws, a
thread keeps the display flipping for the XMB: blue running, green at the end of G2, red failed. */

#include "ps3_host.h"
#include "ps3_system.h"
#include "ps3_video.h"

#include <stdio.h>
#include <unistd.h>

#include <sys/process.h>
#include <sys/thread.h>

/* 1 MB main thread stack, as in the other PS3 ports; the game runs on this thread */
SYS_PROCESS_PARAM(1001, 0x100000);

/* above the game's threads (1001): it sleeps in its vsync waits */
#define DISPLAY_PRIORITY 1000
#define HEARTBEAT_SECONDS 10

int halo_xbox_main(void);
void platform_kernel_initialize(void);
int platform_memory_initialize(void);
int platform_heartbeat(void);
void ps3_host_report_blocked(void);

static volatile int game_status = _ps3_host_status_running;
/* main()'s frame and the game thread's stack, logged at the start */
static uint64_t main_frame;
static uint64_t main_stack_low;
static uint64_t main_stack_high;
static volatile int display_quit;
/* 0 while the RSX starts, then 1, or -1 if it failed */
static volatile int display_state;
static sys_ppu_thread_t display_thread;
static int display_running;
/* the game's thread, for the stall report */
static sys_ppu_thread_t main_thread_id;

void ps3_host_set_status(int status)
{
	game_status = status;
	ps3_log_printf("[ps3] status %s\n", status == _ps3_host_status_running ? "running" :
		status == _ps3_host_status_stopped ? "stopped" : "failed");
}

static unsigned status_color(unsigned frame)
{
	/* a slow pulse: the display thread runs, whatever the game does */
	unsigned level = frame & 0xff;

	if (frame & 0x100)
		level = 0xff - level;
	level = level / 4 + 0x20;
	switch (game_status)
	{
	case _ps3_host_status_stopped: return 0xff000000u | level << 8;
	case _ps3_host_status_failed: return 0xff000000u | level << 16;
	default: return 0xff000000u | level;
	}
}

/* once, at the first stall: whether lv2 still has the main thread (an alignment fault ends it) */
static void main_thread_report(void)
{
	static int reported;
	s32 priority = -1;
	s32 result;

	if (reported)
		return;
	reported = 1;
	result = sysThreadGetPriority(main_thread_id, &priority);
	ps3_log_printf("[ps3] main thread: priority %d (0x%08x)\n", (int)priority, (unsigned)result);
}

static void display_main(void *argument)
{
	unsigned frame = 0;
	double next_heartbeat = ps3_time_seconds() + HEARTBEAT_SECONDS;

	(void)argument;
	if (!ps3_video_init())
	{
		ps3_log_printf("[vid] no display: the XMB may not show over the game\n");
		display_state = -1;
		sysThreadExit(0);
	}
	display_state = 1;
	while (!display_quit)
	{
		ps3_video_begin_frame();
		ps3_video_clear(status_color(frame));
		ps3_video_end_frame();
		frame++;
		if (ps3_time_seconds() >= next_heartbeat)
		{
			ps3_log_printf("[hb] t=%.0fs display frames %u\n", ps3_time_seconds(), frame);
			if (platform_heartbeat())
				main_thread_report();
			ps3_host_report_blocked();
			next_heartbeat += HEARTBEAT_SECONDS;
		}
	}
	ps3_video_shutdown();
	sysThreadExit(0);
}

static void display_start(void)
{
	static char name[] = "halo_display";

	display_running = sysThreadCreate(&display_thread, display_main, NULL, DISPLAY_PRIORITY, 0x10000,
		THREAD_JOINABLE, name) == 0;
	if (!display_running)
	{
		ps3_log_printf("[ps3] cannot start the display thread\n");
		return;
	}
	/* the RSX init allocates: no memory probe may run beside it */
	while (!display_state)
		usleep(10 * 1000);
}

static void display_stop(void)
{
	u64 result;

	if (display_running)
	{
		display_quit = 1;
		sysThreadJoin(display_thread, &result);
		display_running = 0;
	}
}

/* the RSX stops before the process ends, as at the bring-up test's exit */
void ps3_host_exit(int status)
{
	ps3_log_printf("[ps3] exit %d\n", status);
	display_stop();
	ps3_system_shutdown();
	sysProcessExit(status);
}

/* the failure stays on the screen until the XMB exit */
static void wait_for_exit(void)
{
	while (!ps3_exit_requested())
		usleep(100 * 1000);
}

/* the game's halt_and_catch_fire (main.c): its halt screen needs the renderer (G4), and without
it the halt asserts again and exits; here the red screen waits for the XMB exit */
void ps3_host_game_halted(void)
{
	ps3_host_set_status(_ps3_host_status_failed);
	ps3_log_printf("[ps3] the game halted: PS button > Quit Game\n");
	wait_for_exit();
	ps3_host_exit(1);
}

int main(int argc, char *argv[])
{
	static const union { unsigned short value; unsigned char bytes[2]; } byte_order = { 0x0102 };
	int result;

	(void)argc;
	(void)argv;
	ps3_system_init();
	ps3_log_printf("[ps3] Halo CE PS3, built " __DATE__ " " __TIME__ "\n");
	ps3_log_printf("[ps3] sizeof void* %u, long %u, wchar_t %u; %s-endian\n", (unsigned)sizeof(void *),
		(unsigned)sizeof(long), (unsigned)sizeof(wchar_t), byte_order.bytes[0] == 1 ? "big" : "little");
	ps3_memory_report("boot");
	ps3_log_printf("[fs] data root %s\n", ps3_data_root());

	/* first, while no other mmapper area can take 0x80000000 */
	if (!platform_memory_initialize())
	{
		ps3_log_printf("[ps3] FAILED: no Xbox memory window at 0x80000000\n");
		game_status = _ps3_host_status_failed;
		display_start();
		wait_for_exit();
		display_stop();
		ps3_system_shutdown();
		return 1;
	}
	ps3_memory_report("after the Xbox memory window");
	display_start();
	ps3_memory_report("after the RSX init");
	platform_kernel_initialize();
	sysThreadGetId(&main_thread_id);

	ps3_log_printf("[ps3] starting the game\n");
	{
		sys_ppu_thread_stack_t stack;

		if (sysThreadGetStackInformation(&stack) == 0)
		{
			main_stack_low = (uint64_t)stack.addr;
			main_stack_high = main_stack_low + stack.size;
		}
		main_frame = (uint64_t)__builtin_frame_address(0);
		ps3_log_printf("[ps3] main thread stack %08llx..%08llx, main() frame %08llx\n",
			(unsigned long long)main_stack_low, (unsigned long long)main_stack_high, (unsigned long long)main_frame);
	}
	result = halo_xbox_main();
	ps3_log_printf("[ps3] the game returned %d\n", result);

	display_stop();
	ps3_system_shutdown();
	return result;
}
