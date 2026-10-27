/* posix.h's sockets and the process calls of internet play (p2p.c), on ps3_net.h: Winsock's sockaddr_in as p2p.c
fills it, and the error as the thread's last error. Not xnet_ps3.c's Winsock: that one routes the game's traffic. */

#include <string.h>

#include "posix.h"
#include "ps3_net.h"

#ifndef SO_SNDBUF
#define SO_SNDBUF 0x1001
#endif
#ifndef SO_RCVBUF
#define SO_RCVBUF 0x1002
#endif

enum
{
	/* the least socket buffer tried when lv2 refuses a larger one */
	MINIMUM_SOCKET_BUFFER_SIZE = 32 * 1024,
};

/* ---------- errors */

/* a ps3_net.h result: the value, or -1 with the Winsock error as the thread's (posix_socket_last_error) */
static int net_result(int result)
{
	if (result < 0)
	{
		SetLastError((DWORD)-result);
		return -1;
	}
	SetLastError(0);
	return result;
}

int posix_socket_last_error(void)
{
	return (int)GetLastError();
}

/* ---------- addresses */

static int address_take(const void *address, int length, uint32_t *ip, uint16_t *port)
{
	const struct sockaddr_in *in = address;

	if (!in || length < (int)sizeof(*in) || in->sin_family != AF_INET)
	{
		SetLastError(WSAEFAULT);
		return 0;
	}
	*ip = (uint32_t)in->sin_addr.s_addr;
	*port = in->sin_port;
	return 1;
}

static void address_give(void *address, int *length, uint32_t ip, uint16_t port)
{
	struct sockaddr_in *in = address;

	if (!in || !length || *length < (int)sizeof(*in))
		return;
	memset(in, 0, sizeof(*in));
	in->sin_family = AF_INET;
	in->sin_port = port;
	in->sin_addr.s_addr = ip;
	*length = sizeof(*in);
}

/* ---------- sockets */

int posix_socket(int family, int type, int protocol)
{
	if (family != AF_INET)
	{
		SetLastError(WSAEAFNOSUPPORT);
		return -1;
	}
	return net_result(ps3_net_socket(type, protocol));
}

int posix_socket_close(int socket)
{
	return net_result(ps3_net_close(socket));
}

int posix_socket_bind(int socket, const void *address, int address_length)
{
	uint32_t ip;
	uint16_t port;

	if (!address_take(address, address_length, &ip, &port))
		return -1;
	return net_result(ps3_net_bind(socket, ip, port));
}

int posix_socket_connect(int socket, const void *address, int address_length)
{
	uint32_t ip;
	uint16_t port;

	if (!address_take(address, address_length, &ip, &port))
		return -1;
	return net_result(ps3_net_connect(socket, ip, port));
}

int posix_socket_listen(int socket, int backlog)
{
	return net_result(ps3_net_listen(socket, backlog));
}

int posix_socket_accept(int socket, void *address, int *address_length)
{
	uint32_t ip = 0;
	uint16_t port = 0;
	int result = net_result(ps3_net_accept(socket, &ip, &port));

	if (result >= 0)
		address_give(address, address_length, ip, port);
	return result;
}

int posix_socket_send(int socket, const void *buffer, int length, int flags)
{
	if (length < 0)
	{
		SetLastError(WSAEINVAL);
		return -1;
	}
	return net_result(ps3_net_send(socket, buffer, (uint32_t)length, flags));
}

int posix_socket_sendto(int socket, const void *buffer, int length, int flags, const void *address, int address_length)
{
	uint32_t ip;
	uint16_t port;

	if (length < 0)
	{
		SetLastError(WSAEINVAL);
		return -1;
	}
	if (!address_take(address, address_length, &ip, &port))
		return -1;
	return net_result(ps3_net_send_to(socket, buffer, (uint32_t)length, flags, ip, port));
}

int posix_socket_recv(int socket, void *buffer, int length, int flags)
{
	if (length < 0)
	{
		SetLastError(WSAEINVAL);
		return -1;
	}
	return net_result(ps3_net_receive(socket, buffer, (uint32_t)length, flags));
}

int posix_socket_recvfrom(int socket, void *buffer, int length, int flags, void *address, int *address_length)
{
	uint32_t ip = 0;
	uint16_t port = 0;
	int result;

	if (length < 0)
	{
		SetLastError(WSAEINVAL);
		return -1;
	}
	result = net_result(ps3_net_receive_from(socket, buffer, (uint32_t)length, flags, &ip, &port));
	if (result >= 0)
		address_give(address, address_length, ip, port);
	return result;
}

int posix_socket_shutdown(int socket, int how)
{
	return net_result(ps3_net_shutdown(socket, how));
}

int posix_socket_set_nonblocking(int socket, int nonblocking)
{
	return net_result(ps3_net_set_nonblocking(socket, nonblocking));
}

int posix_socket_set_nodelay(int socket)
{
	return net_result(ps3_net_set_nodelay(socket));
}

/* (Winsock's SOL_SOCKET options are lv2's: ps3_net.h) */
int posix_socket_setsockopt(int socket, int level, int name, const void *value, int length)
{
	int result;

	if (!value || length < 0)
	{
		SetLastError(WSAEFAULT);
		return -1;
	}
	result = ps3_net_set_option(socket, level, name, value, (uint32_t)length);
	/* a buffer larger than lv2 allows: the largest it takes, by halves (internet play asks for 1 MB) */
	if (result < 0 && level == SOL_SOCKET && (name == SO_SNDBUF || name == SO_RCVBUF) && length == sizeof(int))
	{
		int size;

		memcpy(&size, value, sizeof(size));
		while (result < 0 && size / 2 >= MINIMUM_SOCKET_BUFFER_SIZE)
		{
			size /= 2;
			result = ps3_net_set_option(socket, level, name, &size, sizeof(size));
		}
	}
	return net_result(result);
}

int posix_socket_getsockopt(int socket, int level, int name, void *value, int *length)
{
	uint32_t size;
	int result;

	if (!value || !length || *length < 0)
	{
		SetLastError(WSAEFAULT);
		return -1;
	}
	size = (uint32_t)*length;
	result = net_result(ps3_net_get_option(socket, level, name, value, &size));
	if (result >= 0)
		*length = (int)size;
	return result;
}

int posix_socket_getsockname(int socket, void *address, int *address_length)
{
	uint32_t ip = 0;
	uint16_t port = 0;
	int result = net_result(ps3_net_local_name(socket, &ip, &port));

	if (result >= 0)
		address_give(address, address_length, ip, port);
	return result;
}

int posix_socket_getpeername(int socket, void *address, int *address_length)
{
	uint32_t ip = 0;
	uint16_t port = 0;
	int result = net_result(ps3_net_peer_name(socket, &ip, &port));

	if (result >= 0)
		address_give(address, address_length, ip, port);
	return result;
}

int posix_socket_select(int *read, int *read_count, int *write, int *write_count, int *error, int *error_count,
	posix_long timeout_seconds, posix_long timeout_microseconds, int infinite)
{
	int milliseconds = infinite ? -1 : (int)(timeout_seconds * 1000 + (timeout_microseconds + 999) / 1000);
	int none = 0;
	int any = (read && read_count && *read_count > 0) || (write && write_count && *write_count > 0) ||
		(error && error_count && *error_count > 0);

	/* (no sockets: a sleep, as poll on Linux; lv2's select refuses none) */
	if (!any)
	{
		if (milliseconds > 0)
			ps3_host_sleep_microseconds((uint64_t)milliseconds * 1000);
		if (read_count)
			*read_count = 0;
		if (write_count)
			*write_count = 0;
		if (error_count)
			*error_count = 0;
		SetLastError(0);
		return 0;
	}
	return net_result(ps3_net_select(read, read && read_count ? read_count : &none, write,
		write && write_count ? write_count : &none, error, error && error_count ? error_count : &none, milliseconds));
}

posix_ulong posix_local_ipv4_address(void)
{
	uint32_t address = 0;

	return ps3_net_local_address(&address) == 1 ? (posix_ulong)address : 0;
}

void posix_random_bytes(void *buffer, posix_ulong size)
{
	/* (keys come of these: none from the timebase, as posix.h's contract says) */
	if (!ps3_net_secure_random(buffer, (uint32_t)size))
	{
		platform_log("lv2 gave no random numbers: internet play cannot make its keys");
		ps3_host_exit(1);
	}
}

posix_ulong posix_resolve_ipv4(const char *host)
{
	return host ? (posix_ulong)ps3_net_resolve(host) : 0;
}

/* ---------- the process and the desktop: none on the PS3 (as on Android) */

int posix_command_line_argument(int index, char *buffer, posix_ulong size)
{
	(void)index;
	if (size)
		buffer[0] = 0;
	return 0;
}

posix_ulong posix_process_id(void)
{
	return 1;
}

int posix_register_url_scheme(const char *scheme, const char *description)
{
	(void)scheme;
	(void)description;
	return 0;
}

int posix_user_secret(unsigned char *secret, int size)
{
	(void)secret;
	(void)size;
	return 0;
}

/* (no Discord client runs on a PS3) */
int posix_discord_connect(void)
{
	return -1;
}

int posix_discord_write(int handle, const void *buffer, int length)
{
	(void)handle;
	(void)buffer;
	(void)length;
	return -1;
}

int posix_discord_read(int handle, void *buffer, int length)
{
	(void)handle;
	(void)buffer;
	(void)length;
	return -1;
}

void posix_discord_close(int handle)
{
	(void)handle;
}
