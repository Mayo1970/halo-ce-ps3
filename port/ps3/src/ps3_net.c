/* The lv2 side of ps3_net.h, from the hardware-tested patterns of ioQuake3-PS3 and xashPS3 (sin_len set,
SO_NBIO, poll). libnet's own calls: their error is the thread's (net_errno), not newlib's one errno. */

#include "ps3_net.h"
#include "ps3_system.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ppu-types.h>
#include <lv2/system.h>
#include <net/net.h>
#include <net/netctl.h>
#include <sys/mutex.h>
#include <sys/thread.h>
#include <sysmodule/sysmodule.h>

#define WSABASEERR 10000
#define WINSOCK_ERROR(net_error) (-(WSABASEERR + (net_error)))
/* BSD's, which PSL1GHT's headers leave out */
#define PS3_NET_TCP_NODELAY 1
/* the descriptors a select polls without an allocation */
#define SELECT_STACK_ENTRIES 256

/* ---------- errors */

/* Winsock's codes are its base plus BSD's numbers, which lv2's net errors are. The first errors but
would-block go to the log too: the game tells no one of its transport errors */
static int winsock_error(const char *call)
{
	static int logged;
	s32 error = net_errno;

	if (error != NET_EWOULDBLOCK && error != NET_EINPROGRESS && logged < 32)
	{
		logged++;
		ps3_log_printf("[net] %s: lv2 error %d%s\n", call, (int)error, logged == 32 ? " (no more logged)" : "");
	}
	switch (error)
	{
	case NET_EINTR:
	case NET_EBADF:
	case NET_EACCES:
	case NET_EFAULT:
	case NET_EINVAL:
	case NET_EMFILE:
		return WINSOCK_ERROR(error);
	case NET_EPERM: return WINSOCK_ERROR(NET_EACCES);
	case NET_ENFILE: return WINSOCK_ERROR(NET_EMFILE);
	case NET_ENOMEM: return WINSOCK_ERROR(NET_ENOBUFS);
	/* a send on a connection the other end reset: the game takes Winsock's reset as the connection lost */
	case NET_EPIPE: return WINSOCK_ERROR(NET_ECONNRESET);
	}
	/* (others: Winsock's invalid argument) */
	return error >= NET_EWOULDBLOCK && error <= NET_EREMOTE ? WINSOCK_ERROR(error) : WINSOCK_ERROR(NET_EINVAL);
}

/* ---------- sockets */

static void address_make(struct sockaddr_in *in, uint32_t address, uint16_t port)
{
	memset(in, 0, sizeof(*in));
	/* lv2 fails an address with sin_len 0 (ENOENT on sendto, ioQuake3-PS3) */
	in->sin_len = sizeof(*in);
	in->sin_family = AF_INET;
	in->sin_port = port;
	in->sin_addr.s_addr = address;
}

int ps3_net_socket(int type, int protocol)
{
	s32 result = netSocket(AF_INET, type, protocol);

	return result < 0 ? winsock_error(__func__) : result;
}

int ps3_net_close(int socket)
{
	return netClose(socket) < 0 ? winsock_error(__func__) : 0;
}

int ps3_net_bind(int socket, uint32_t address, uint16_t port)
{
	struct sockaddr_in in;

	address_make(&in, address, port);
	return netBind(socket, (struct sockaddr *)&in, sizeof(in)) < 0 ? winsock_error(__func__) : 0;
}

int ps3_net_connect(int socket, uint32_t address, uint16_t port)
{
	struct sockaddr_in in;

	address_make(&in, address, port);
	if (netConnect(socket, (struct sockaddr *)&in, sizeof(in)) >= 0)
		return 0;
	/* Winsock's would-block, which the game waits on before it selects for writing (connect_endpoint) */
	if (net_errno == NET_EINPROGRESS)
		return WINSOCK_ERROR(NET_EWOULDBLOCK);
	return winsock_error(__func__);
}

int ps3_net_listen(int socket, int backlog)
{
	return netListen(socket, backlog) < 0 ? winsock_error(__func__) : 0;
}

int ps3_net_accept(int socket, uint32_t *address, uint16_t *port)
{
	struct sockaddr_in in;
	socklen_t length = sizeof(in);
	s32 result;

	memset(&in, 0, sizeof(in));
	result = netAccept(socket, (struct sockaddr *)&in, &length);
	if (result < 0)
		return winsock_error(__func__);
	*address = in.sin_addr.s_addr;
	*port = in.sin_port;
	return result;
}

int ps3_net_send(int socket, const void *buffer, uint32_t length, int flags)
{
	ssize_t result = netSend(socket, buffer, length, flags);

	return result < 0 ? winsock_error(__func__) : (int)result;
}

int ps3_net_send_to(int socket, const void *buffer, uint32_t length, int flags, uint32_t address, uint16_t port)
{
	struct sockaddr_in in;
	ssize_t result;

	address_make(&in, address, port);
	result = netSendTo(socket, buffer, length, flags, (struct sockaddr *)&in, sizeof(in));
	return result < 0 ? winsock_error(__func__) : (int)result;
}

int ps3_net_receive(int socket, void *buffer, uint32_t length, int flags)
{
	ssize_t result = netRecv(socket, buffer, length, flags);

	return result < 0 ? winsock_error(__func__) : (int)result;
}

/* (a datagram longer than the buffer comes cut, not as Winsock's WSAEMSGSIZE: its header's length then
differs from what came, and the game drops it, network_connection_idle) */
int ps3_net_receive_from(int socket, void *buffer, uint32_t length, int flags, uint32_t *address, uint16_t *port)
{
	struct sockaddr_in in;
	socklen_t in_length = sizeof(in);
	ssize_t result;

	memset(&in, 0, sizeof(in));
	result = netRecvFrom(socket, buffer, length, flags, (struct sockaddr *)&in, &in_length);
	if (result < 0)
		return winsock_error(__func__);
	*address = in.sin_addr.s_addr;
	*port = in.sin_port;
	return (int)result;
}

int ps3_net_shutdown(int socket, int how)
{
	return netShutdown(socket, how) < 0 ? winsock_error(__func__) : 0;
}

int ps3_net_set_nonblocking(int socket, int nonblocking)
{
	int value = nonblocking ? 1 : 0;

	return netSetSockOpt(socket, SOL_SOCKET, SO_NBIO, &value, sizeof(value)) < 0 ? winsock_error(__func__) : 0;
}

int ps3_net_set_nodelay(int socket)
{
	int value = 1;

	return netSetSockOpt(socket, IPPROTO_TCP, PS3_NET_TCP_NODELAY, &value, sizeof(value)) < 0 ? winsock_error(__func__) : 0;
}

int ps3_net_set_option(int socket, int level, int name, const void *value, uint32_t length)
{
	return netSetSockOpt(socket, level, name, value, length) < 0 ? winsock_error(__func__) : 0;
}

int ps3_net_get_option(int socket, int level, int name, void *value, uint32_t *length)
{
	socklen_t size = *length;

	if (netGetSockOpt(socket, level, name, value, &size) < 0)
		return winsock_error(__func__);
	*length = size;
	return 0;
}

int ps3_net_local_name(int socket, uint32_t *address, uint16_t *port)
{
	struct sockaddr_in in;
	socklen_t length = sizeof(in);

	memset(&in, 0, sizeof(in));
	if (netGetSockName(socket, (struct sockaddr *)&in, &length) < 0)
		return winsock_error(__func__);
	*address = in.sin_addr.s_addr;
	*port = in.sin_port;
	return 0;
}

int ps3_net_peer_name(int socket, uint32_t *address, uint16_t *port)
{
	struct sockaddr_in in;
	socklen_t length = sizeof(in);

	memset(&in, 0, sizeof(in));
	if (netGetPeerName(socket, (struct sockaddr *)&in, &length) < 0)
		return winsock_error(__func__);
	*address = in.sin_addr.s_addr;
	*port = in.sin_port;
	return 0;
}

/* ---------- select */

/* the socket's pending error (SO_ERROR, which reading clears), read once a call for both lists */
static int pending_error(struct pollfd const *descriptors, int *errors, int index)
{
	int other;

	if (errors[index] >= 0)
		return errors[index];
	for (other = 0; other < index; other++)
	{
		if (descriptors[other].fd == descriptors[index].fd && errors[other] >= 0)
			return errors[index] = errors[other];
	}
	{
		int value = 0;
		socklen_t length = sizeof(value);

		errors[index] = netGetSockOpt(descriptors[index].fd, SOL_SOCKET, SO_ERROR, &value, &length) < 0 ? 0 : value;
	}
	return errors[index];
}

/* the events poll gave the socket in any list: a connect that failed shows as room to write */
static short socket_events(struct pollfd const *descriptors, int count, int socket)
{
	short events = 0;
	int index;

	for (index = 0; index < count; index++)
	{
		if (descriptors[index].fd == socket)
			events |= descriptors[index].revents;
	}
	return events;
}

/* poll, not lv2's select (fd_set without PSL1GHT's bit 30). Write: room once connected; error: urgent data
or a failed connect, as Winsock has it (the game waits on that: connect_endpoint) */
int ps3_net_select(int *read, int *read_count, int *write, int *write_count, int *error, int *error_count,
	int timeout_ms)
{
	static const short events[3] = { POLLIN, POLLOUT, POLLPRI };
	struct pollfd stack_descriptors[SELECT_STACK_ENTRIES];
	int stack_errors[SELECT_STACK_ENTRIES];
	struct pollfd *descriptors = stack_descriptors;
	int *errors = stack_errors;
	int *lists[3] = { read, write, error };
	int *counts[3] = { read_count, write_count, error_count };
	int count = 0;
	int entry = 0;
	int list, index;
	int result;

	for (list = 0; list < 3; list++)
	{
		if (!lists[list] || !counts[list] || *counts[list] <= 0)
			lists[list] = NULL;
		else
			count += *counts[list];
	}
	/* (as Winsock: no sockets is no select) */
	if (!count)
		return WINSOCK_ERROR(NET_EINVAL);
	if (count > SELECT_STACK_ENTRIES)
	{
		descriptors = malloc(sizeof(*descriptors) * (size_t)count);
		errors = malloc(sizeof(*errors) * (size_t)count);
		if (!descriptors || !errors)
		{
			free(descriptors);
			free(errors);
			return WINSOCK_ERROR(NET_ENOBUFS);
		}
	}
	for (list = 0; list < 3; list++)
	{
		for (index = 0; lists[list] && index < *counts[list]; index++, entry++)
		{
			descriptors[entry].fd = lists[list][index];
			descriptors[entry].events = events[list];
			descriptors[entry].revents = 0;
			errors[entry] = -1;
		}
	}
	result = netPoll(descriptors, (nfds_t)count, timeout_ms < 0 ? -1 : timeout_ms);
	if (result < 0)
	{
		result = winsock_error(__func__);
	}
	else
	{
		for (index = 0; index < count; index++)
		{
			if (descriptors[index].revents & POLLNVAL)
				result = WINSOCK_ERROR(NET_ENOTSOCK);
		}
	}
	if (result >= 0)
	{
		result = 0;
		entry = 0;
		for (list = 0; list < 3; list++)
		{
			int kept = 0;

			for (index = 0; lists[list] && index < *counts[list]; index++, entry++)
			{
				short ready = descriptors[entry].revents;
				int is_ready;

				if (list == 0)
				{
					is_ready = (ready & (POLLIN | POLLHUP | POLLERR)) != 0;
				}
				else if (list == 1)
				{
					is_ready = (ready & POLLOUT) && !pending_error(descriptors, errors, entry);
				}
				else
				{
					is_ready = (ready & POLLPRI) ||
						((socket_events(descriptors, count, descriptors[entry].fd) & (POLLOUT | POLLERR | POLLHUP)) &&
							pending_error(descriptors, errors, entry));
				}
				if (is_ready)
					lists[list][kept++] = descriptors[entry].fd;
			}
			if (lists[list])
				*counts[list] = kept;
			result += kept;
		}
	}
	if (descriptors != stack_descriptors)
	{
		free(descriptors);
		free(errors);
	}
	return result;
}

/* ---------- this PS3's address */

static int netctl_ready(void)
{
	/* 0: not asked, 1: ready, -1: failed */
	static int state;
	s32 result;

	if (state)
		return state > 0;
	result = sysModuleLoad(SYSMODULE_NETCTL);
	if (result != 0 && result != (s32)SYSMODULE_ERR_DUPLICATE)
	{
		ps3_log_printf("[net] sysModuleLoad(NETCTL)=0x%08x\n", (unsigned)result);
		state = -1;
		return 0;
	}
	/* (NOT_TERMINATED: initialized already) */
	result = netCtlInit();
	if (result != 0 && result != (s32)NET_CTL_ERROR_NOT_TERMINATED)
	{
		ps3_log_printf("[net] netCtlInit=0x%08x\n", (unsigned)result);
		state = -1;
		return 0;
	}
	state = 1;
	return 1;
}

/* a dotted quad in network byte order, 0 for none */
static uint32_t parse_ipv4(const char *text)
{
	uint32_t value = 0;
	int part;

	for (part = 0; part < 4; part++)
	{
		uint32_t number = 0;
		int digits = 0;

		while (*text >= '0' && *text <= '9' && digits < 3)
		{
			number = number * 10 + (uint32_t)(*text++ - '0');
			digits++;
		}
		if (!digits || number > 255 || (part < 3 && *text++ != '.'))
			return 0;
		value = value << 8 | number;
	}
	return *text ? 0 : value;
}

int ps3_net_local_address(uint32_t *address)
{
	/* the menus ask every frame: netctl once a second */
	static int cached_result;
	static uint32_t cached_address;
	static double cached_time = -1.0;
	double now = ps3_time_seconds();
	union net_ctl_info info;
	s32 state = NET_CTL_STATE_Disconnected;
	uint32_t found = 0;
	int result = 0;

	if (cached_time >= 0.0 && now - cached_time < 1.0)
	{
		*address = cached_address;
		return cached_result;
	}
	if (netctl_ready() && netCtlGetState(&state) == 0)
	{
		if (state == NET_CTL_STATE_Connecting || state == NET_CTL_STATE_IPObtaining)
		{
			result = -1;
		}
		else if (state == NET_CTL_STATE_IPObtained && netCtlGetInfo(NET_CTL_INFO_IP_ADDRESS, &info) == 0)
		{
			info.ip_address[sizeof(info.ip_address) - 1] = 0;
			found = parse_ipv4(info.ip_address);
			result = found != 0;
		}
	}
	cached_address = found;
	cached_result = result;
	cached_time = now;
	*address = found;
	return result;
}

/* ---------- host names */

uint32_t ps3_net_resolve(const char *host)
{
	static sys_mutex_t lock;
	/* 0: none, 1: being made, 2: made, -1: failed */
	static volatile int lock_made;
	uint32_t address = parse_ipv4(host);
	uint32_t local_address;
	struct net_hostent *entry;

	/* (offline, a lookup would wait long for nothing) */
	if (address || !host[0] || ps3_net_local_address(&local_address) != 1)
		return address;
	/* (one lookup at a time: the answer is the sprx's own) */
	if (__sync_bool_compare_and_swap(&lock_made, 0, 1))
	{
		sys_mutex_attr_t attributes;
		int made;

		sysMutexAttrInitialize(attributes);
		made = sysMutexCreate(&lock, &attributes) == 0;
		__sync_synchronize();
		lock_made = made ? 2 : -1;
	}
	while (lock_made == 1)
		sysThreadYield();
	if (lock_made > 0)
		sysMutexLock(lock, 0);
	entry = netGetHostByName(host);
	/* (the system's 32-bit pointers: a list of addresses) */
	if (entry && entry->h_addrtype == AF_INET && entry->h_length == 4 && entry->h_addr_list)
	{
		const uint32_t *list = (const uint32_t *)(uintptr_t)entry->h_addr_list;

		if (list[0])
			memcpy(&address, (const void *)(uintptr_t)list[0], sizeof(address));
	}
	if (lock_made > 0)
		sysMutexUnlock(lock);
	if (!address)
		ps3_log_printf("[net] %s: no address (DNS)\n", host);
	return address;
}

/* ---------- random numbers */

int ps3_net_secure_random(void *buffer, uint32_t size)
{
	uint8_t *cursor = buffer;

	while (size)
	{
		uint32_t chunk = size < RANDOM_NUMBER_MAX_SIZE ? size : RANDOM_NUMBER_MAX_SIZE;

		if (sysGetRandomNumber(cursor, chunk) != 0)
			return 0;
		cursor += chunk;
		size -= chunk;
	}
	return 1;
}

void ps3_net_random(void *buffer, uint32_t size)
{
	static uint64_t state;
	uint8_t *cursor = buffer;

	while (size)
	{
		uint32_t chunk = size < RANDOM_NUMBER_MAX_SIZE ? size : RANDOM_NUMBER_MAX_SIZE;

		if (sysGetRandomNumber(cursor, chunk) != 0)
			break;
		cursor += chunk;
		size -= chunk;
	}
	/* (lv2 refused: the timebase's, enough for nonces) */
	if (size && !state)
		state = (uint64_t)(ps3_time_seconds() * 1e9) | 1;
	while (size)
	{
		state ^= state << 13;
		state ^= state >> 7;
		state ^= state << 17;
		*cursor++ = (uint8_t)state;
		size--;
	}
}
