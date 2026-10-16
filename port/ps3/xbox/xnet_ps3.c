/* Winsock and XNet on the PSL1GHT sockets (ps3_net.c), from port/linux/src/xnet.c without debug.network_latency:
internet play's peers (p2p.c) through its stand-ins, else plain IPv4, enough for system link. */

#include <stdio.h>
#include <string.h>

#include "port_config.h"
#include "p2p.h"
#include "ps3_net.h"

/* the game units' halo_ws_fd_set: FD_SETSIZE HALO_PORT_FD_SETSIZE (256), as ps3_platform.h sets it */
typedef char halo_ws_fd_set_size_assert[sizeof(halo_ws_fd_set) == 0x404 ? 1 : -1];
typedef char winsock_sockaddr_in_size_assert[sizeof(struct sockaddr_in) == 16 ? 1 : -1];

/* Winsock's, which the SDK headers here leave out */
#ifndef SO_KEEPALIVE
#define SO_KEEPALIVE 0x0008
#endif
#ifndef SO_ERROR
#define SO_ERROR 0x1007
#endif
#ifndef SO_TYPE
#define SO_TYPE 0x1008
#endif
#ifndef IPPROTO_TCP
#define IPPROTO_TCP 6
#endif

enum
{
	MAXIMUM_BROADCAST_TARGETS = 256,
	/* a system link game's other machines (include/halo_port_limits.h) */
	P2P_BROADCAST_PEERS = 127,
	/* the least socket buffer tried when lv2 refuses a larger one */
	MINIMUM_SOCKET_BUFFER_SIZE = 32 * 1024,
};

/* ---------- address settings */

/* network.address and network.broadcast (config.toml, as on the Linux build), read once, in network
byte order */
static struct
{
	int read;
	int has_local_address;
	unsigned long local_address;
	int broadcast_count;
	unsigned long broadcast_targets[MAXIMUM_BROADCAST_TARGETS];
} net_settings;

/* a dotted quad filling [text, end), spaces around it allowed; the address in network byte order */
static int parse_ipv4(const char *text, const char *end, unsigned long *address)
{
	unsigned long value = 0;
	int part;

	while (text < end && (*text == ' ' || *text == '\t'))
		text++;
	while (end > text && (end[-1] == ' ' || end[-1] == '\t'))
		end--;
	for (part = 0; part < 4; part++)
	{
		unsigned long number = 0;
		int digits = 0;

		while (text < end && *text >= '0' && *text <= '9' && digits < 3)
		{
			number = number * 10 + (unsigned long)(*text++ - '0');
			digits++;
		}
		if (!digits || number > 255)
			return 0;
		value = value << 8 | number;
		if (part < 3)
		{
			if (text >= end || *text != '.')
				return 0;
			text++;
		}
	}
	if (text != end)
		return 0;
	*address = halo_ws_htonl(value);
	return 1;
}

static void net_settings_read(void)
{
	const char *text;

	if (net_settings.read)
		return;
	text = config_string("network.address");
	if (text && *text)
	{
		unsigned long address;

		/* neither 0.0.0.0 nor 255.255.255.255 is a machine's address */
		if (parse_ipv4(text, text + strlen(text), &address) && address != 0 && address != INADDR_BROADCAST)
		{
			net_settings.local_address = address;
			net_settings.has_local_address = 1;
		}
		else
		{
			platform_log("network.address \"%s\" is not a usable IPv4 address: ignored", text);
		}
	}
	text = config_string("network.broadcast");
	while (text && *text)
	{
		const char *end = text + strcspn(text, ",");
		unsigned long address;

		if (parse_ipv4(text, end, &address) && address != 0)
		{
			if (net_settings.broadcast_count < MAXIMUM_BROADCAST_TARGETS)
				net_settings.broadcast_targets[net_settings.broadcast_count++] = address;
		}
		else if (end > text)
		{
			platform_log("network.broadcast: %.*s is not an IPv4 address: ignored", (int)(end - text), text);
		}
		text = *end ? end + 1 : end;
	}
	if (net_settings.has_local_address && !net_settings.broadcast_count)
	{
		platform_log("network.address without network.broadcast: sockets bound to one address receive no "
			"broadcasts, so this PS3 sees other machines' games and searches only if they list its address");
	}
	net_settings.read = 1;
}

/* the address to use in place of INADDR_ANY, if network.address is set */
static int local_address_setting(unsigned long *address)
{
	net_settings_read();
	if (!net_settings.has_local_address)
		return 0;
	*address = net_settings.local_address;
	return 1;
}

/* 127.0.0.1, which the game uses for itself (a host joins its own game through it) */
static unsigned long loopback_address(void)
{
	return halo_ws_htonl(0x7F000001);
}

/* ---------- Winsock addresses */

/* an IPv4 address's parts; FALSE, with Winsock's error set, for none */
static int address_from_winsock(const struct sockaddr *address, int address_length, unsigned long *ip,
	unsigned short *port)
{
	const struct sockaddr_in *in = (const struct sockaddr_in *)address;

	if (!address || address_length < (int)sizeof(*in))
	{
		WSASetLastError(WSAEFAULT);
		return 0;
	}
	if (in->sin_family != AF_INET)
	{
		WSASetLastError(WSAEAFNOSUPPORT);
		return 0;
	}
	*ip = in->sin_addr.s_addr;
	*port = in->sin_port;
	return 1;
}

static void address_to_winsock(unsigned long ip, unsigned short port, struct sockaddr *address,
	int *address_length)
{
	struct sockaddr_in in;
	int size;

	if (!address || !address_length || *address_length <= 0)
		return;
	memset(&in, 0, sizeof(in));
	in.sin_family = AF_INET;
	in.sin_port = port;
	in.sin_addr.s_addr = ip;
	size = *address_length < (int)sizeof(in) ? *address_length : (int)sizeof(in);
	memcpy(address, &in, (size_t)size);
	*address_length = size;
}

/* a destination of 127.0.0.1 means the network.address address */
static unsigned long outgoing_address(unsigned long ip)
{
	unsigned long local;

	return ip == loopback_address() && local_address_setting(&local) ? local : ip;
}

/* traffic from the network.address address comes from 127.0.0.1 */
static unsigned long incoming_address(unsigned long ip)
{
	unsigned long local;

	return local_address_setting(&local) && ip == local ? loopback_address() : ip;
}

/* ---------- internet play's peers (p2p.h) */

/* an internet play peer's virtual address, in 100.64.0.0/10 */
static int peer_address(unsigned long ip)
{
	return (halo_ws_ntohl(ip) & 0xFFC00000) == 0x64400000;
}

/* a peer's destination goes to its stand-in (1); -1, with WSAEHOSTUNREACH, if the peer cannot be reached now; 0 if
it is not a peer's. connecting: the UDP socket connected to it, else -1 */
static int peer_outgoing_address(int stream, int connecting, unsigned long *ip, unsigned short *port)
{
	int result = p2p_outgoing(stream, connecting, ip, port);

	if (result < 0)
		WSASetLastError(WSAEHOSTUNREACH);
	return result;
}

static int socket_type(SOCKET socket)
{
	int type = SOCK_STREAM;
	uint32_t length = sizeof(type);

	ps3_net_get_option((int)socket, SOL_SOCKET, SO_TYPE, &type, &length);
	return type;
}

/* the local port the game's socket is bound to, or 0 if it is not yet */
static unsigned short socket_port(SOCKET socket)
{
	uint32_t ip = 0;
	uint16_t port = 0;

	return ps3_net_local_name((int)socket, &ip, &port) < 0 ? 0 : port;
}

/* tells internet play the local port of the game's socket (bound, given one by a connection or a send, or listening) */
static void note_socket_port(SOCKET socket, int listening)
{
	uint32_t ip = 0;
	uint16_t port = 0;
	int stream;

	if (ps3_net_local_name((int)socket, &ip, &port) < 0 || !port)
		return;
	stream = socket_type(socket) == SOCK_STREAM;
	/* (one bound to this machine alone, as the telnet console's, is not for peers to reach, nor hosting) */
	if (ip == loopback_address())
		p2p_port_taken(stream, port);
	else
		p2p_socket_port((int)socket, stream, listening, port);
}

/* traffic from a peer's stand-in comes from the peer, and from the network.address address from 127.0.0.1 */
static unsigned long peer_incoming_address(int stream, unsigned long ip, unsigned short *port)
{
	unsigned long peer = ip;
	unsigned short peer_port = *port;

	if (p2p_incoming(stream, &peer, &peer_port))
	{
		*port = peer_port;
		return peer;
	}
	return incoming_address(ip);
}

/* ---------- Winsock */

static int winsock_result(int result)
{
	if (result < 0)
	{
		WSASetLastError(-result);
		return SOCKET_ERROR;
	}
	return result;
}

/* as on Windows: Winsock's error is the thread's last error */
int WSAAPI WSAGetLastError(void)
{
	return (int)GetLastError();
}

void WSAAPI WSASetLastError(int error)
{
	SetLastError((DWORD)error);
}

/* internet play's brokers beside config.toml (p2p_signal.c reads them), written at each start as an update does */
static void brokers_file_write(void)
{
	static char const brokers[] =
#include "brokers_txt.h"
		;
	char path[256];
	int32_t descriptor;
	uint64_t written = 0;

	snprintf(path, sizeof(path), "%s/brokers.txt", platform_save_root());
	if (ps3_host_file_open(path, PS3_HOST_O_WRONLY | PS3_HOST_O_CREAT | PS3_HOST_O_TRUNC, &descriptor) != 0)
	{
		platform_log("Internet play: %s cannot be written", path);
		return;
	}
	if (ps3_host_file_write(descriptor, brokers, sizeof(brokers) - 1, &written) != 0 || written != sizeof(brokers) - 1)
		platform_log("Internet play: %s was not all written", path);
	ps3_host_file_close(descriptor);
}

int WSAAPI WSAStartup(WORD version_requested, LPWSADATA data)
{
	uint32_t address = 0;
	int state = ps3_net_local_address(&address);
	unsigned long local;

	/* here, before the game's network threads start */
	net_settings_read();
	brokers_file_write();
	p2p_initialize(local_address_setting(&local) ? local : loopback_address());
	if (data)
	{
		memset(data, 0, sizeof(*data));
		data->wVersion = version_requested;
		data->wHighVersion = MAKEWORD(2, 2);
		data->iMaxSockets = 64;
		data->iMaxUdpDg = 1264;
	}
	platform_log("Winsock: PSL1GHT sockets, this PS3 at %u.%u.%u.%u%s", (unsigned)(address >> 24),
		(unsigned)(address >> 16 & 0xFF), (unsigned)(address >> 8 & 0xFF), (unsigned)(address & 0xFF),
		state < 0 ? " (netctl still getting an address)" : state ? "" : " (no network)");
	return 0;
}

int WSAAPI WSACleanup(void)
{
	return 0;
}

SOCKET WSAAPI halo_ws_socket(int family, int type, int protocol)
{
	int result;

	if (family != AF_INET)
	{
		WSASetLastError(WSAEAFNOSUPPORT);
		return INVALID_SOCKET;
	}
	result = ps3_net_socket(type, protocol);
	if (result < 0)
	{
		WSASetLastError(-result);
		return INVALID_SOCKET;
	}
	/* (the game's connections: every tick's messages go at once) */
	if (type == SOCK_STREAM)
		ps3_net_set_nodelay(result);
	return (SOCKET)result;
}

int WSAAPI halo_ws_closesocket(SOCKET socket)
{
	p2p_socket_closed((int)socket, socket_type(socket) == SOCK_DGRAM ? socket_port(socket) : 0);
	return winsock_result(ps3_net_close((int)socket));
}

int WSAAPI halo_ws_bind(SOCKET socket, const struct sockaddr *address, int address_length)
{
	unsigned long ip;
	unsigned short port;
	unsigned long override;
	int result;

	if (!address_from_winsock(address, address_length, &ip, &port))
		return SOCKET_ERROR;
	if (ip == INADDR_ANY && local_address_setting(&override))
		ip = override;
	result = ps3_net_bind((int)socket, ip, port);
	if (result == 0)
		note_socket_port(socket, 0);
	return winsock_result(result);
}

int WSAAPI halo_ws_connect(SOCKET socket, const struct sockaddr *address, int address_length)
{
	unsigned long ip;
	unsigned short port;
	unsigned long override;
	int result;

	if (!address_from_winsock(address, address_length, &ip, &port))
		return SOCKET_ERROR;
	/* an internet play peer's TCP or UDP port: its stand-in */
	switch (peer_address(ip) ? peer_outgoing_address(socket_type(socket) == SOCK_STREAM, (int)socket, &ip, &port) : 0)
	{
	case -1:
		return SOCKET_ERROR;
	case 0:
		ip = outgoing_address(ip);
		break;
	}
	/* a connection from an unbound socket leaves from whichever address the route picks; with
	network.address it leaves from that address */
	if (local_address_setting(&override))
	{
		uint32_t bound_ip = 0;
		uint16_t bound_port = 0;

		if (ps3_net_local_name((int)socket, &bound_ip, &bound_port) < 0 || (bound_port == 0 && bound_ip == 0))
			ps3_net_bind((int)socket, override, 0);
	}
	result = ps3_net_connect((int)socket, ip, port);
	/* the port the system gave it (a connection under way has one too) */
	note_socket_port(socket, 0);
	return winsock_result(result);
}

int WSAAPI halo_ws_listen(SOCKET socket, int backlog)
{
	int result = ps3_net_listen((int)socket, backlog);

	/* (and it listens for connections while it hosts) */
	if (result == 0)
		note_socket_port(socket, 1);
	return winsock_result(result);
}

SOCKET WSAAPI halo_ws_accept(SOCKET socket, struct sockaddr *address, int *address_length)
{
	uint32_t ip = 0;
	uint16_t port = 0;
	unsigned short peer_port;
	unsigned long peer;
	int result = ps3_net_accept((int)socket, &ip, &port);

	if (result < 0)
	{
		WSASetLastError(-result);
		return INVALID_SOCKET;
	}
	ps3_net_set_nodelay(result);
	peer_port = port;
	peer = peer_incoming_address(1, ip, &peer_port);
	address_to_winsock(peer, peer_port, address, address_length);
	return (SOCKET)result;
}

int WSAAPI halo_ws_send(SOCKET socket, const char *buffer, int length, int flags)
{
	if (length < 0)
	{
		WSASetLastError(WSAEINVAL);
		return SOCKET_ERROR;
	}
	return winsock_result(ps3_net_send((int)socket, buffer, (uint32_t)length, flags));
}

/* a broadcast: to network.broadcast's addresses instead (one datagram each, sent if any is), and to every internet
play peer, onto the tunnel at once (else through the stand-ins) */
static int broadcast_send(SOCKET socket, const char *buffer, int length, int flags, unsigned short port,
	unsigned short source_port)
{
	unsigned long targets[P2P_BROADCAST_PEERS];
	unsigned short ports[P2P_BROADCAST_PEERS];
	int peer_count;
	int result;
	int index;

	if (net_settings.broadcast_count)
	{
		result = 0;
		for (index = 0; index < net_settings.broadcast_count; index++)
		{
			int sent = ps3_net_send_to((int)socket, buffer, (uint32_t)length, flags,
				net_settings.broadcast_targets[index], port);

			if (sent >= 0 || index == 0)
				result = sent;
		}
	}
	else
	{
		result = ps3_net_send_to((int)socket, buffer, (uint32_t)length, flags, INADDR_BROADCAST, port);
	}
	/* (after the send above, which binds the socket if it was not) */
	if (!source_port)
	{
		source_port = socket_port(socket);
		if (source_port)
			note_socket_port(socket, 0);
	}
	if (source_port)
	{
		if (p2p_broadcast_datagram(source_port, port, buffer, length) > 0 && result < 0)
			result = length;
		return result;
	}
	peer_count = p2p_broadcast_targets(port, targets, ports, P2P_BROADCAST_PEERS);
	for (index = 0; index < peer_count; index++)
	{
		int sent = ps3_net_send_to((int)socket, buffer, (uint32_t)length, flags, targets[index], ports[index]);

		if (result < 0 && sent >= 0)
			result = sent;
	}
	return result;
}

int WSAAPI halo_ws_sendto(SOCKET socket, const char *buffer, int length, int flags,
	const struct sockaddr *address, int address_length)
{
	/* 0: not bound yet, which the first send binds it */
	unsigned short source_port = socket_port(socket);
	unsigned long ip;
	unsigned short port;
	int result;

	if (length < 0)
	{
		WSASetLastError(WSAEINVAL);
		return SOCKET_ERROR;
	}
	if (!address_from_winsock(address, address_length, &ip, &port))
		return SOCKET_ERROR;
	net_settings_read();
	if (ip == INADDR_BROADCAST)
		return winsock_result(broadcast_send(socket, buffer, length, flags, port, source_port));
	/* a peer's: onto the tunnel at once, from the socket's port (one not bound yet goes through a stand-in) */
	if (peer_address(ip) && source_port)
	{
		switch (p2p_send_datagram(source_port, ip, port, buffer, length))
		{
		case 1:
			return length;
		case -1:
			WSASetLastError(WSAEHOSTUNREACH);
			return SOCKET_ERROR;
		}
	}
	switch (peer_address(ip) ? peer_outgoing_address(0, -1, &ip, &port) : 0)
	{
	case -1:
		return SOCKET_ERROR;
	case 0:
		ip = outgoing_address(ip);
		break;
	}
	result = ps3_net_send_to((int)socket, buffer, (uint32_t)length, flags, ip, port);
	/* the port the send gave it (one that failed binds it too) */
	if (!source_port)
		note_socket_port(socket, 0);
	return winsock_result(result);
}

int WSAAPI halo_ws_recv(SOCKET socket, char *buffer, int length, int flags)
{
	if (length < 0)
	{
		WSASetLastError(WSAEINVAL);
		return SOCKET_ERROR;
	}
	return winsock_result(ps3_net_receive((int)socket, buffer, (uint32_t)length, flags));
}

int WSAAPI halo_ws_recvfrom(SOCKET socket, char *buffer, int length, int flags,
	struct sockaddr *address, int *address_length)
{
	uint32_t ip = 0;
	uint16_t port = 0;
	int result;

	if (length < 0)
	{
		WSASetLastError(WSAEINVAL);
		return SOCKET_ERROR;
	}
	result = ps3_net_receive_from((int)socket, buffer, (uint32_t)length, flags, &ip, &port);
	if (result >= 0)
	{
		unsigned short peer_port = port;
		unsigned long peer = peer_incoming_address(0, ip, &peer_port);

		address_to_winsock(peer, peer_port, address, address_length);
	}
	return winsock_result(result);
}

int WSAAPI halo_ws_shutdown(SOCKET socket, int how)
{
	return winsock_result(ps3_net_shutdown((int)socket, how));
}

/* FIONBIO alone: lv2 has no ioctl (its non-blocking mode is SO_NBIO), and the game asks for no other */
int WSAAPI halo_ws_ioctlsocket(SOCKET socket, long command, u_long *argument)
{
	if ((unsigned long)command == (unsigned long)FIONBIO && argument)
		return winsock_result(ps3_net_set_nonblocking((int)socket, *argument != 0));
	WSASetLastError(WSAEINVAL);
	return SOCKET_ERROR;
}

/* the SOL_SOCKET options lv2 numbers as Winsock does, with the same values (an int) */
static int known_socket_option(int name)
{
	switch (name)
	{
	case SO_REUSEADDR:
	case SO_KEEPALIVE:
	case SO_BROADCAST:
	case SO_SNDBUF:
	case SO_RCVBUF:
	case SO_ERROR:
	case SO_TYPE:
		return 1;
	}
	return 0;
}

int WSAAPI halo_ws_setsockopt(SOCKET socket, int level, int name, const char *value, int length)
{
	int result;

	if (!value || length < 0)
	{
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	/* Xbox-only options (and those lv2 has otherwise) have nothing to do here */
	if (level == SOL_SOCKET ? !known_socket_option(name) : level != IPPROTO_TCP)
		return 0;
	result = ps3_net_set_option((int)socket, level, name, value, (uint32_t)length);
	/* a buffer larger than lv2 allows: the largest it takes, by halves (the game asks for 1 MB) */
	if (result < 0 && level == SOL_SOCKET && (name == SO_SNDBUF || name == SO_RCVBUF) && length == sizeof(int))
	{
		static int logged;
		int asked;
		int size;

		memcpy(&asked, value, sizeof(asked));
		size = asked;
		while (result < 0 && size / 2 >= MINIMUM_SOCKET_BUFFER_SIZE)
		{
			size /= 2;
			result = ps3_net_set_option((int)socket, level, name, &size, sizeof(size));
		}
		if (logged < 4)
		{
			logged++;
			platform_log("Winsock: %s of %d bytes refused; %d %s", name == SO_SNDBUF ? "SO_SNDBUF" : "SO_RCVBUF", asked,
				size, result < 0 ? "refused too" : "taken");
		}
	}
	return winsock_result(result);
}

int WSAAPI halo_ws_getsockopt(SOCKET socket, int level, int name, char *value, int *length)
{
	uint32_t size;
	int result;

	if (!value || !length || *length < 0)
	{
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	if (level == SOL_SOCKET ? !known_socket_option(name) : level != IPPROTO_TCP)
	{
		WSASetLastError(WSAENOPROTOOPT);
		return SOCKET_ERROR;
	}
	size = (uint32_t)*length;
	result = ps3_net_get_option((int)socket, level, name, value, &size);
	if (result >= 0)
		*length = (int)size;
	return winsock_result(result);
}

int WSAAPI halo_ws_getsockname(SOCKET socket, struct sockaddr *address, int *address_length)
{
	uint32_t ip = 0;
	uint16_t port = 0;
	int result = ps3_net_local_name((int)socket, &ip, &port);

	if (result >= 0)
		address_to_winsock(ip, port, address, address_length);
	return winsock_result(result);
}

int WSAAPI halo_ws_getpeername(SOCKET socket, struct sockaddr *address, int *address_length)
{
	uint32_t ip = 0;
	uint16_t port = 0;
	int result = ps3_net_peer_name((int)socket, &ip, &port);

	if (result >= 0)
	{
		unsigned short peer_port = port;
		unsigned long peer = peer_incoming_address(1, ip, &peer_port);

		address_to_winsock(peer, peer_port, address, address_length);
	}
	return winsock_result(result);
}

/* ---------- select and fd_set */

static int descriptors_from_set(halo_ws_fd_set *set, int *descriptors)
{
	u_int index;

	for (index = 0; index < set->fd_count && index < FD_SETSIZE; index++)
		descriptors[index] = (int)set->fd_array[index];
	return (int)index;
}

static void set_from_descriptors(halo_ws_fd_set *set, const int *descriptors, int count)
{
	int index;

	for (index = 0; index < count; index++)
		set->fd_array[index] = (SOCKET)descriptors[index];
	set->fd_count = (u_int)count;
}

/* (a select that finds nothing leaves the last error, as Winsock: after a connect under way, still
its would-block, which the game reads as not connected yet) */
int WSAAPI halo_ws_select(int descriptor_count, halo_ws_fd_set *read_set, halo_ws_fd_set *write_set,
	halo_ws_fd_set *error_set, const struct halo_ws_timeval *timeout)
{
	int read[FD_SETSIZE], write[FD_SETSIZE], error[FD_SETSIZE];
	int read_count = read_set ? descriptors_from_set(read_set, read) : 0;
	int write_count = write_set ? descriptors_from_set(write_set, write) : 0;
	int error_count = error_set ? descriptors_from_set(error_set, error) : 0;
	int timeout_ms = -1;
	int result;

	(void)descriptor_count;
	if (timeout)
	{
		__int64 milliseconds = (__int64)timeout->tv_sec * 1000 + ((__int64)timeout->tv_usec + 999) / 1000;

		timeout_ms = milliseconds < 0 ? 0 : milliseconds > 0x7FFFFFFF ? 0x7FFFFFFF : (int)milliseconds;
	}
	result = ps3_net_select(read, &read_count, write, &write_count, error, &error_count, timeout_ms);
	if (result < 0)
		return winsock_result(result);
	if (read_set)
		set_from_descriptors(read_set, read, read_count);
	if (write_set)
		set_from_descriptors(write_set, write, write_count);
	if (error_set)
		set_from_descriptors(error_set, error, error_count);
	return result;
}

int PASCAL __WSAFDIsSet(SOCKET socket, halo_ws_fd_set *set)
{
	u_int index;

	for (index = 0; index < set->fd_count; index++)
	{
		if (set->fd_array[index] == socket)
			return 1;
	}
	return 0;
}

/* ---------- byte order and addresses: network order is the PS3's own */

u_long WSAAPI halo_ws_htonl(u_long value)
{
	return value;
}

u_long WSAAPI halo_ws_ntohl(u_long value)
{
	return value;
}

u_short WSAAPI halo_ws_htons(u_short value)
{
	return value;
}

u_short WSAAPI halo_ws_ntohs(u_short value)
{
	return value;
}

unsigned long WSAAPI halo_ws_inet_addr(const char *text)
{
	unsigned long address;

	return text && parse_ipv4(text, text + strlen(text), &address) ? address : INADDR_NONE;
}

/* ---------- XNet */

INT WSAAPI XNetStartup(const XNetStartupParams *parameters)
{
	(void)parameters;
	return 0;
}

INT WSAAPI XNetCleanup(void)
{
	return 0;
}

INT WSAAPI XNetRandom(BYTE *buffer, UINT size)
{
	ps3_net_random(buffer, size);
	return 0;
}

INT WSAAPI XNetCreateKey(XNKID *key_identifier, XNKEY *key)
{
	ps3_net_random(key_identifier, sizeof(*key_identifier));
	ps3_net_random(key, sizeof(*key));
	return 0;
}

INT WSAAPI XNetRegisterKey(const XNKID *key_identifier, const XNKEY *key)
{
	(void)key_identifier;
	(void)key;
	return 0;
}

INT WSAAPI XNetUnregisterKey(const XNKID *key_identifier)
{
	(void)key_identifier;
	return 0;
}

INT WSAAPI XNetXnAddrToInAddr(const XNADDR *address, const XNKID *key_identifier, IN_ADDR *result)
{
	unsigned long peer;

	(void)key_identifier;
	/* an internet play peer's XNADDR carries its identifier, else its machine's real address */
	if (p2p_peer_address(address->abEnet, &peer))
		result->s_addr = peer;
	else
		*result = address->ina;
	return 0;
}

/* this machine's system link address: network.address, else netctl's (-1 while it gets one) */
static int title_address(unsigned long *address)
{
	uint32_t ip = 0;
	int result;

	if (local_address_setting(address))
		return 1;
	result = ps3_net_local_address(&ip);
	*address = ip;
	return result;
}

DWORD WSAAPI XNetGetTitleXnAddr(XNADDR *address)
{
	unsigned long ip = 0;
	int result = title_address(&ip);

	memset(address, 0, sizeof(*address));
	address->bSizeOfStruct = sizeof(*address);
	/* (the game asks again for up to 10 s: transport_initialize) */
	if (result < 0)
	{
		Sleep(10);
		return XNET_GET_XNADDR_PENDING;
	}
	address->ina.s_addr = ip;
	/* (internet play's peers know this machine by it) */
	memcpy(address->abEnet, p2p_identifier(), sizeof(address->abEnet));
	return result > 0 ? (XNET_GET_XNADDR_ETHERNET | XNET_GET_XNADDR_DHCP) : XNET_GET_XNADDR_ETHERNET;
}

DWORD WSAAPI XNetGetEthernetLinkStatus(void)
{
	unsigned long ip = 0;

	return title_address(&ip) > 0 ?
		(XNET_ETHERNET_LINK_ACTIVE | XNET_ETHERNET_LINK_100MBPS | XNET_ETHERNET_LINK_FULL_DUPLEX) : 0;
}
