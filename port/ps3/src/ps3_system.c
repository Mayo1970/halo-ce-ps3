/* PS3 system glue, from the hardware-tested patterns of ioQuake3-PS3
(ps3_sys.c, ps3_main.c) and xashPS3 (sys_ps3.c). */

#include "ps3_system.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ppu-asm.h>
#include <ppu-lv2.h>
#include <ppu-types.h>
#include <arpa/inet.h>
#include <net/net.h>
#include <netinet/in.h>
#include <sys/memory.h>
#include <sys/mutex.h>
#include <sys/process.h>
#include <sys/socket.h>
#include <sys/systime.h>
#include <sys/thread.h>
#include <sysmodule/sysmodule.h>
#include <sysutil/sysutil.h>

#ifndef PS3_LOG_IP
#define PS3_LOG_IP "192.168.178.99"
#endif
#define PS3_LOG_PORT 18194
#define PS3_DATA_FOLDER "HaloCE"
#define PS3_USB_PORTS 7

static int log_socket = -1;
static struct sockaddr_in log_address;
static sys_mutex_t log_mutex;
static int log_mutex_ready;

static volatile int exit_flag;
static volatile int monitor_quit;
static sys_ppu_thread_t monitor_thread;
static int monitor_running;

static u64 time_base;
static double timebase_frequency = 79800000.0;

static char data_root[64];

static uint32_t memory_lowest_bytes = 0xffffffffu;
static double memory_lowest_seconds;

/* ---------- log */

static void log_send(const char *text, int length)
{
#ifdef PS3_LOG
	if (log_socket >= 0 && length > 0)
		sendto(log_socket, text, (size_t)length, 0, (struct sockaddr *)&log_address, sizeof(log_address));
#endif
}

int ps3_log_vprintf(const char *format, va_list ap)
{
	/* static, not on the stack: deep call chains overran thread stacks in xashPS3 */
	static char buffer[2048];
	int length;

#ifndef PS3_LOG
	/* the retail build (DEBUG=0): nothing formatted, nothing sent */
	return 0;
#endif
	if (log_mutex_ready)
		sysMutexLock(log_mutex, 0);
	length = vsnprintf(buffer, sizeof(buffer), format, ap);
	if (length > (int)sizeof(buffer) - 1)
		length = sizeof(buffer) - 1;
	log_send(buffer, length);
	if (log_mutex_ready)
		sysMutexUnlock(log_mutex);
	return length;
}

int ps3_log_printf(const char *format, ...)
{
	va_list ap;
	int length;

	va_start(ap, format);
	length = ps3_log_vprintf(format, ap);
	va_end(ap);
	return length;
}

/* stdout has no console on a retail PS3: the link routes these here (-Wl,--wrap) */
int __wrap_printf(const char *format, ...)
{
	va_list ap;
	int length;

	va_start(ap, format);
	length = ps3_log_vprintf(format, ap);
	va_end(ap);
	return length;
}

int __wrap_vprintf(const char *format, va_list ap)
{
	return ps3_log_vprintf(format, ap);
}

int __wrap_puts(const char *text)
{
	return ps3_log_printf("%s\n", text);
}

int __wrap_putchar(int character)
{
	char byte = (char)character;

	log_send(&byte, 1);
	return character;
}

static void log_init(void)
{
	sys_mutex_attr_t attributes;

#ifndef PS3_LOG
	return;
#endif
	sysMutexAttrInitialize(attributes);
	log_mutex_ready = sysMutexCreate(&log_mutex, &attributes) == 0;

	log_socket = socket(AF_INET, SOCK_DGRAM, 0);
	if (log_socket < 0)
		return;
	memset(&log_address, 0, sizeof(log_address));
	/* PSL1GHT's sockaddr_in has a BSD sin_len; with 0, sendto fails with ENOENT */
	log_address.sin_len = sizeof(log_address);
	log_address.sin_family = AF_INET;
	log_address.sin_port = htons(PS3_LOG_PORT);
	log_address.sin_addr.s_addr = inet_addr(PS3_LOG_IP);
}

/* ---------- XMB exit */

static volatile unsigned overlay_events;
static volatile int overlay_drawing, overlay_menu;
static volatile int events_checking;

static void sysutil_callback(u64 status, u64 parameter, void *user_data)
{
	(void)parameter;
	(void)user_data;
	/* only latch it: the main loop shuts down at a normal call depth */
	if (status == SYSUTIL_EXIT_GAME)
		exit_flag = 1;
	/* the system draws over the game (the XMB in the game, a dialog) */
	if (status == SYSUTIL_DRAW_BEGIN || status == SYSUTIL_DRAW_END || status == SYSUTIL_MENU_OPEN ||
		status == SYSUTIL_MENU_CLOSE)
		overlay_events++;
	if (status == SYSUTIL_DRAW_BEGIN || status == SYSUTIL_DRAW_END)
		overlay_drawing = status == SYSUTIL_DRAW_BEGIN;
	if (status == SYSUTIL_MENU_OPEN || status == SYSUTIL_MENU_CLOSE)
		overlay_menu = status == SYSUTIL_MENU_OPEN;
}

/* the monitor's 100 ms pass alone saw the XMB up to 6 frames late; one thread at a time, the other skips */
void ps3_system_check_events(void)
{
	if (__sync_lock_test_and_set(&events_checking, 1))
		return;
	sysUtilCheckCallback();
	__sync_lock_release(&events_checking);
}

int ps3_exit_requested(void)
{
	return exit_flag;
}

unsigned ps3_system_overlay_events(void)
{
	return overlay_events;
}

int ps3_system_overlay_active(void)
{
	return overlay_drawing || overlay_menu;
}

/* Pumps the sysutil events while the main thread is busy (map loads). An exit
that the game does not answer in 5 s ends the process, else the XMB hangs. */
static void monitor_main(void *argument)
{
	unsigned ticks_since_exit = 0;

	(void)argument;
	while (!monitor_quit)
	{
		ps3_system_check_events();
		if (exit_flag && ++ticks_since_exit > 50)
		{
			ps3_log_printf("[ps3] no shutdown 5 s after the XMB exit: ending the process\n");
			sysProcessExit(0);
		}
		usleep(100 * 1000);
	}
	sysThreadExit(0);
}

/* ---------- memory */

/* Container creation fails cleanly when the size does not fit; an
over-budget malloc can hang lv2 instead of returning NULL. */
unsigned ps3_memory_free_mb(void)
{
	unsigned low = 1, high = 256, best = 0;

	while (low <= high)
	{
		unsigned middle = (low + high) / 2;
		sys_mem_container_t container;

		if (sysMemContainerCreate(&container, (size_t)middle << 20) == 0)
		{
			sysMemContainerDestroy(container);
			best = middle;
			low = middle + 1;
		}
		else
		{
			high = middle - 1;
		}
	}
	return best;
}

/* sys_memory_get_user_memory_size: PSL1GHT has its number (lv2/syscalls.h), not a wrapper */
LV2_SYSCALL memory_user_size(u32 *information)
{
	lv2syscall1(352, (u64)information);
	return_to_user_prog(s32);
}

int ps3_memory_user(uint32_t *total, uint32_t *available)
{
	u32 information[2] __attribute__((aligned(8)));
	s32 result = memory_user_size(information);

	if (result != 0)
		return result;
	*total = information[0];
	*available = information[1];
	return 0;
}

/* lv2's count beside the probe, which it must match */
void ps3_memory_report(const char *label)
{
	uint32_t total = 0, available = 0;
	unsigned probe = ps3_memory_free_mb();
	int result = ps3_memory_user(&total, &available);

	ps3_log_printf("[mem] %s: %u MB free; lv2: %.1f MB free of %.1f MB (0x%08x)\n", label, probe,
		available / 1048576.0, total / 1048576.0, (unsigned)result);
}

void ps3_memory_sample(void)
{
	uint32_t total, available;

	if (ps3_memory_user(&total, &available) == 0 && available < memory_lowest_bytes)
	{
		memory_lowest_bytes = available;
		memory_lowest_seconds = ps3_time_seconds();
	}
}

void ps3_memory_lowest(uint32_t *bytes, double *seconds)
{
	*bytes = memory_lowest_bytes;
	*seconds = memory_lowest_seconds;
}

/* ---------- time */

double ps3_time_seconds(void)
{
	return (double)(__gettime() - time_base) / timebase_frequency;
}

/* ---------- data root */

static int has_maps_folder(const char *folder)
{
	DIR *directory = opendir(folder);
	struct dirent *entry;
	int found = 0;

	if (!directory)
		return 0;
	while (!found && (entry = readdir(directory)) != NULL)
		found = strcasecmp(entry->d_name, "maps") == 0;
	closedir(directory);
	return found;
}

static void find_data_root(void)
{
	char candidate[sizeof(data_root)];
	int port;

	snprintf(data_root, sizeof(data_root), "/dev_hdd0/data/" PS3_DATA_FOLDER);
	if (has_maps_folder(data_root))
		return;
	for (port = 0; port < PS3_USB_PORTS; port++)
	{
		snprintf(candidate, sizeof(candidate), "/dev_usb%03d/" PS3_DATA_FOLDER, port);
		if (has_maps_folder(candidate))
		{
			snprintf(data_root, sizeof(data_root), "%s", candidate);
			return;
		}
	}
	/* no game data yet: make the HDD folder, ready for maps/ */
	mkdir("/dev_hdd0/data", 0777);
	mkdir(data_root, 0777);
}

const char *ps3_data_root(void)
{
	return data_root;
}

/* ---------- init and shutdown */

void ps3_system_init(void)
{
	static char monitor_name[] = "halo_monitor";
	int net_result;

	timebase_frequency = (double)sysGetTimebaseFrequency();
	time_base = __gettime();
	sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sysutil_callback, NULL);

	/* the net module must load before netInitialize (ioQuake3-PS3, xashPS3) */
	sysModuleLoad(SYSMODULE_NET);
	net_result = netInitialize();
	log_init();
	ps3_log_printf("[ps3] netInitialize=%d, log to " PS3_LOG_IP ":%d\n", net_result, PS3_LOG_PORT);

	/* priority 1000: above the game threads (1001), which can keep both PPE threads busy; lower
	numbers run first, and it sleeps 100 ms per pass */
	if (sysThreadCreate(&monitor_thread, monitor_main, NULL, 1000, 0x10000, THREAD_JOINABLE, monitor_name) == 0)
		monitor_running = 1;
	else
		ps3_log_printf("[ps3] cannot start the monitor thread: no XMB exit while busy\n");

	find_data_root();
}

void ps3_system_shutdown(void)
{
	if (monitor_running)
	{
		u64 result;

		monitor_quit = 1;
		sysThreadJoin(monitor_thread, &result);
		monitor_running = 0;
	}
	sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
	ps3_log_printf("[ps3] shut down\n");
	if (log_socket >= 0)
	{
		close(log_socket);
		log_socket = -1;
	}
	/* no netDeinitialize: it froze the shutdown on hardware in xashPS3 */
}
