/* The console game list (ps3_game_list.h) and the signed legacy table, on a thread of their own: the list over
HTTPS (the game's port/console/https on ps3_net.c), else plain HTTP, as the Wii U's wiiu_online.c and wiiu_https.c. */

#include "ps3_game_list.h"
#include "ps3_host.h"
#include "ps3_net.h"
#include "ps3_system.h"

#include <stdio.h>
#include <string.h>

#include "console_https.h"

/* the game's (port/linux/src/delta.c with HALO_CONSOLE_HTTPS, port_config.c) */
int delta_legacy_refresh(void);
void config_folder(char *path, size_t size);

#define FETCH_TIMEOUT 15000
#define ATTEMPTS 3
#define RETRY_WAIT 10000
/* the signed table again this often, and this long after a failure (delta.c's desktop intervals) */
#define TABLE_REFRESH (4ul * 60 * 60 * 1000)
#define TABLE_RETRY (30ul * 60 * 1000)
/* the thread's own frames, CONSOLE_HTTPS_STACK_SIZE for the TLS session, and the table's document (delta.h's
DELTA_REFRESH_STACK_SIZE, 22 KB, with room) */
#define ONLINE_THREAD_STACK (128 * 1024 + CONSOLE_HTTPS_STACK_SIZE + 64 * 1024)

/* ps3_net.h's results: minus Winsock's codes */
#define NET_WOULD_BLOCK (-10035)
#define NET_RESET (-10054)

enum
{
	_list_idle,
	_list_fetching,
	_list_done,
	_list_failed
};

/* the thread writes fetched, then the state (release); the game thread takes it once done (acquire), and only
then may a new fetch start */
static int list_state = _list_idle;
static int list_taken = 1;
static struct game_list fetched;
static int list_error;
static char response_buffer[GAME_LIST_RESPONSE_SIZE];
/* the game thread's: the last list that came, kept while a refresh fails */
static struct game_list shown;
static int shown_lists;
static int shown_error;
static int shown_state = PS3_GAME_LIST_NONE;
/* the list came over HTTPS; HTTPS refused for good (a key or a name): plain HTTP for the rest of the run */
static int list_secure;
static int https_refused;

static struct ps3_host_lock *https_lock_handle;
static char pins_path[300];
static int table_busy;
static int table_asked;
static unsigned long table_next;

static void set_state(int state)
{
	__atomic_store_n(&list_state, state, __ATOMIC_RELEASE);
}

static int get_state(void)
{
	return __atomic_load_n(&list_state, __ATOMIC_ACQUIRE);
}

static unsigned long now_ms(void)
{
	return (unsigned long)(ps3_host_microseconds() / 1000);
}

/* ---------- sockets */

/* until the socket can be read (or written) or the deadline: 1 ready */
static int wait_ready(int handle, int write, unsigned long deadline)
{
	for (;;)
	{
		int handles[1], count = 1;
		long left = (long)(deadline - now_ms());

		if (left <= 0 || ps3_host_exit_requested())
			return 0;
		if (left > 250)
			left = 250;
		handles[0] = handle;
		if (ps3_net_select(write ? NULL : handles, write ? NULL : &count, write ? handles : NULL,
			write ? &count : NULL, NULL, NULL, (int)left) > 0 && count > 0)
		{
			return 1;
		}
	}
}

/* a TCP connection to address:port (network order) within the deadline: the socket, else -1 */
static int connect_to(uint32_t address, uint16_t port, unsigned long deadline)
{
	int handle = ps3_net_socket(1, 0);
	int result;

	if (handle < 0)
		return -1;
	ps3_net_set_nonblocking(handle, 1);
	result = ps3_net_connect(handle, address, port);
	if (result != 0 && (result != NET_WOULD_BLOCK || !wait_ready(handle, 1, deadline)))
	{
		ps3_net_close(handle);
		return -1;
	}
	return handle;
}

/* ---------- plain HTTP */

/* whether the first size bytes hold the headers' end; the response's whole size in *total if they give a
Content-Length (else 0) */
static int header_end(const char *data, unsigned long size, unsigned long *total)
{
	struct game_list_response response;
	unsigned long index;

	for (index = 1; index < size; index++)
	{
		if (data[index] == '\n' && (data[index - 1] == '\n' || (index >= 2 && data[index - 1] == '\r' &&
			data[index - 2] == '\n')))
		{
			int result = game_list_parse_response(data, size, &response);

			*total = 0;
			if ((result == GAME_LIST_OK || result == GAME_LIST_ERROR_BODY_TRUNCATED) && response.content_length >= 0)
				*total = index + 1 + (unsigned long)response.content_length;
			return 1;
		}
	}
	return 0;
}

/* GET path from address:port with Host: host, the whole response into buffer (never past size) within
timeout_ms */
static int http_get(uint32_t address, uint16_t port, const char *host, const char *path, char *buffer,
	unsigned long size, unsigned long *received, unsigned long timeout_ms)
{
	char request[512];
	unsigned long deadline = now_ms() + timeout_ms;
	unsigned long sent = 0, total = 0;
	int headers_done = 0, length, handle, result = GAME_LIST_OK;

	*received = 0;
	length = snprintf(request, sizeof(request), "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: HaloPS3/1\r\n"
		"Accept: text/plain\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n", path, host);
	if (length <= 0 || length >= (int)sizeof(request))
		return GAME_LIST_ERROR_SEND;
	handle = connect_to(address, port, deadline);
	if (handle < 0)
		return GAME_LIST_ERROR_CONNECT;
	while (sent < (unsigned long)length)
	{
		int count;

		if (!wait_ready(handle, 1, deadline))
		{
			result = GAME_LIST_ERROR_TIMEOUT;
			break;
		}
		count = ps3_net_send(handle, request + sent, (uint32_t)(length - (int)sent), 0);
		if (count == NET_WOULD_BLOCK)
			continue;
		if (count <= 0)
		{
			result = GAME_LIST_ERROR_SEND;
			break;
		}
		sent += (unsigned long)count;
	}
	/* to the server's close, or the Content-Length's end */
	while (result == GAME_LIST_OK)
	{
		int count;

		if (*received >= size)
		{
			result = GAME_LIST_ERROR_TOO_LARGE;
			break;
		}
		if (!wait_ready(handle, 0, deadline))
		{
			result = GAME_LIST_ERROR_TIMEOUT;
			break;
		}
		count = ps3_net_receive(handle, buffer + *received, (uint32_t)(size - *received), 0);
		if (count == NET_WOULD_BLOCK)
			continue;
		if (count < 0)
		{
			result = GAME_LIST_ERROR_RECEIVE;
			break;
		}
		if (count == 0)
			break;
		*received += (unsigned long)count;
		if (!headers_done)
			headers_done = header_end(buffer, *received, &total);
		if (headers_done && total && *received >= total)
			break;
	}
	ps3_net_close(handle);
	return result;
}

/* the pin document's plain GET (console_https.h's plain_get) */
static int plain_get(const char *host, const char *path, char *response, unsigned long size, unsigned long *received,
	unsigned long timeout_ms)
{
	uint32_t address = ps3_net_resolve(host);

	*received = 0;
	return address && http_get(address, 80, host, path, response, size, received, timeout_ms) == GAME_LIST_OK &&
		*received > 0;
}

/* ---------- HTTPS's port */

static int https_connect(const char *host, unsigned short port, unsigned long timeout_ms, console_https_socket *out)
{
	uint32_t address = ps3_net_resolve(host);
	int handle;

	if (!address)
	{
		ps3_log_printf("[online] https: %s did not resolve\n", host);
		return 0;
	}
	handle = connect_to(address, port, now_ms() + timeout_ms);
	if (handle < 0)
	{
		ps3_log_printf("[online] https: could not connect to %s\n", host);
		return 0;
	}
	*out = (console_https_socket)(long)handle;
	return 1;
}

static int https_send(console_https_socket socket, const unsigned char *data, int size, unsigned long timeout_ms)
{
	int handle = (int)(long)socket;
	unsigned long deadline = now_ms() + timeout_ms;

	for (;;)
	{
		int count;

		if (!wait_ready(handle, 1, deadline))
			return CONSOLE_HTTPS_IO_TIMEOUT;
		count = ps3_net_send(handle, data, (uint32_t)size, 0);
		if (count > 0)
			return count;
		if (count != NET_WOULD_BLOCK)
			return -1;
	}
}

static int https_receive(console_https_socket socket, unsigned char *buffer, int size, unsigned long timeout_ms)
{
	int handle = (int)(long)socket;
	unsigned long deadline = now_ms() + timeout_ms;

	for (;;)
	{
		int count;

		if (!wait_ready(handle, 0, deadline))
			return CONSOLE_HTTPS_IO_TIMEOUT;
		count = ps3_net_receive(handle, buffer, (uint32_t)size, 0);
		if (count >= 0)
			return count;
		if (count == NET_WOULD_BLOCK)
			continue;
		/* (a server that resets the connection at the end, as a close) */
		return count == NET_RESET ? 0 : -1;
	}
}

static void https_close(console_https_socket socket)
{
	ps3_net_close((int)(long)socket);
}

/* lv2's random numbers alone: a request fails without them */
static int https_random(unsigned char *buffer, int size)
{
	return ps3_net_secure_random(buffer, (uint32_t)size) ? 1 : 0;
}

static void https_lock(void)
{
	ps3_host_lock_enter(https_lock_handle);
}

static void https_unlock(void)
{
	ps3_host_lock_leave(https_lock_handle);
}

static void https_log(const char *text)
{
	ps3_log_printf("[online] %s\n", text);
}

static struct console_https_port ps3_https_port =
{
	https_connect, https_send, https_receive, https_close, https_random, now_ms, https_lock, https_unlock,
	https_log, "HaloPS3/1", plain_get, NULL,
};

/* ---------- the list */

/* the list over HTTPS from the site (its key pinned): GAME_LIST_OK and the list, else plain HTTP is asked */
static int fetch_https(char *buffer)
{
	struct game_list_response response;
	unsigned long received = 0;
	int result;

	result = console_https_request(CONSOLE_HTTPS_SITE, GAME_LIST_PATH, NULL, NULL, buffer, GAME_LIST_RESPONSE_SIZE,
		&received, FETCH_TIMEOUT);
	if (result != CONSOLE_HTTPS_OK)
	{
		ps3_log_printf("[online] game list: HTTPS: %s; asking over plain HTTP\n", console_https_error_string(result));
		if (result == CONSOLE_HTTPS_ERROR_KEY || result == CONSOLE_HTTPS_ERROR_NAME)
		{
			https_refused = 1;
			ps3_log_printf("[online] game list: HTTPS refused: plain HTTP for the rest of this run\n");
		}
		return GAME_LIST_ERROR_CONNECT;
	}
	result = game_list_parse_response(buffer, received, &response);
	if (result == GAME_LIST_OK && response.status != 200)
		result = GAME_LIST_ERROR_HTTP;
	if (result == GAME_LIST_OK)
		result = game_list_parse(response.body, response.body_size, &fetched);
	if (result != GAME_LIST_OK && result != GAME_LIST_ERROR_FORMAT)
		ps3_log_printf("[online] game list: HTTPS: %s; asking over plain HTTP\n", game_list_error_string(result));
	if (result == GAME_LIST_OK)
		list_secure = 1;
	return result;
}

static int fetch_once(char *buffer, int attempt)
{
	struct game_list_response response;
	unsigned long received = 0;
	uint32_t address;
	int result;

	list_secure = 0;
	/* (HTTPS first, on each fetch's first try; anything but a list falls back) */
	if (attempt == 1 && !https_refused && console_https_available())
	{
		result = fetch_https(buffer);
		if (result == GAME_LIST_OK || result == GAME_LIST_ERROR_FORMAT)
			return result;
	}
	address = ps3_net_resolve(GAME_LIST_HOST);
	if (!address)
		return GAME_LIST_ERROR_RESOLVE;
	result = http_get(address, GAME_LIST_PORT, GAME_LIST_HOST, GAME_LIST_PATH, buffer, GAME_LIST_RESPONSE_SIZE,
		&received, FETCH_TIMEOUT);
	if (result == GAME_LIST_OK)
		result = game_list_parse_response(buffer, received, &response);
	if (result == GAME_LIST_OK && response.status != 200)
		result = GAME_LIST_ERROR_HTTP;
	if (result == GAME_LIST_OK)
		result = game_list_parse(response.body, response.body_size, &fetched);
	return result;
}

/* the signed legacy table when it is due; one thread at a time (a later list's thread may be here already) */
static void table_refresh(void)
{
	int expected = 0;

	if (!console_https_available() || (table_asked && (long)(now_ms() - table_next) < 0))
		return;
	if (!__atomic_compare_exchange_n(&table_busy, &expected, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return;
	table_asked = 1;
	table_next = now_ms() + (delta_legacy_refresh() ? TABLE_REFRESH : TABLE_RETRY);
	__atomic_store_n(&table_busy, 0, __ATOMIC_RELEASE);
}

static void online_thread(void *argument)
{
	static int https_started;
	int attempt, result = GAME_LIST_ERROR_CONNECT;

	(void)argument;
	/* (HTTPS set up by the first thread: its pins beside config.toml) */
	if (!https_started)
	{
		https_started = 1;
		https_lock_handle = ps3_host_lock_create(0);
		config_folder(pins_path, sizeof(pins_path) - 32);
		strcat(pins_path, "console_pins.signed");
		ps3_https_port.pins_path = pins_path;
		if (https_lock_handle)
			console_https_initialize(&ps3_https_port);
	}
	for (attempt = 1; attempt <= ATTEMPTS && !ps3_host_exit_requested(); attempt++)
	{
		result = fetch_once(response_buffer, attempt);
		if (result == GAME_LIST_OK || result == GAME_LIST_ERROR_FORMAT)
			break;
		ps3_log_printf("[online] game list: try %d of %d: %s\n", attempt, ATTEMPTS, game_list_error_string(result));
		if (attempt < ATTEMPTS)
			ps3_host_sleep_microseconds((uint64_t)RETRY_WAIT * 1000);
	}
	list_error = result;
	if (result == GAME_LIST_OK)
		ps3_log_printf("[online] game list: %d games (format %d) from %s%s\n", fetched.count, fetched.format,
			list_secure ? CONSOLE_HTTPS_SITE " over HTTPS" : GAME_LIST_HOST " over plain HTTP",
			fetched.skipped ? ", some lines left out" : "");
	set_state(result == GAME_LIST_OK ? _list_done : _list_failed);
	/* (after the list, so that the list is never held up by it) */
	table_refresh();
}

void ps3_online_start(void)
{
	ps3_game_list_request();
}

void ps3_game_list_request(void)
{
	/* (not while a fetch runs, or before its list is taken) */
	if (get_state() == _list_fetching || !list_taken)
		return;
	list_taken = 0;
	shown_state = PS3_GAME_LIST_FETCHING;
	set_state(_list_fetching);
	if (ps3_host_thread_create(online_thread, NULL, ONLINE_THREAD_STACK, PS3_HOST_PRIORITY_NORMAL, "game list") != 0)
	{
		ps3_log_printf("[online] game list: no thread\n");
		list_error = GAME_LIST_ERROR_CONNECT;
		set_state(_list_failed);
	}
}

/* a list that came, taken (the game thread's) */
static void take(void)
{
	int state = get_state();

	if (list_taken || (state != _list_done && state != _list_failed))
		return;
	list_taken = 1;
	if (state == _list_failed)
	{
		shown_error = list_error;
		shown_state = PS3_GAME_LIST_FAILED;
		ps3_log_printf("[online] game list: could not get the list from %s or %s (%s)\n", CONSOLE_HTTPS_SITE,
			GAME_LIST_HOST, game_list_error_string(list_error));
		return;
	}
	memcpy(&shown, &fetched, sizeof(shown));
	shown_error = GAME_LIST_OK;
	shown_state = PS3_GAME_LIST_READY;
	shown_lists++;
}

const struct game_list *ps3_game_list_get(int *lists)
{
	take();
	if (lists)
		*lists = shown_lists;
	return shown_lists ? &shown : NULL;
}

int ps3_game_list_state(void)
{
	take();
	return shown_state;
}

const char *ps3_game_list_error(void)
{
	return game_list_error_string(shown_error);
}
