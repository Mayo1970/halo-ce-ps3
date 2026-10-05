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

/* ---------- log */

static void log_send(const char *text, int length)
{
	if (log_socket >= 0 && length > 0)
		sendto(log_socket, text, (size_t)length, 0, (struct sockaddr *)&log_address, sizeof(log_address));
}

int ps3_log_vprintf(const char *format, va_list ap)
{
	/* static, not on the stack: deep call chains overran thread stacks in xashPS3 */
	static char buffer[2048];
	int length;

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

static void sysutil_callback(u64 status, u64 parameter, void *user_data)
{
	(void)parameter;
	(void)user_data;
	/* only latch it: the main loop shuts down at a normal call depth */
	if (status == SYSUTIL_EXIT_GAME)
		exit_flag = 1;
}

int ps3_exit_requested(void)
{
	return exit_flag;
}

/* Pumps the sysutil events while the main thread is busy (map loads). An exit
that the game does not answer in 5 s ends the process, else the XMB hangs. */
static void monitor_main(void *argument)
{
	unsigned ticks_since_exit = 0;

	(void)argument;
	while (!monitor_quit)
	{
		sysUtilCheckCallback();
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

void ps3_memory_report(const char *label)
{
	ps3_log_printf("[mem] %s: %u MB free\n", label, ps3_memory_free_mb());
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
