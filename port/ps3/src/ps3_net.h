/* PS3_NET.H: the PSL1GHT sockets of the Xbox layer's Winsock (xnet_ps3.c); addresses and ports in network
order (the PS3's own). A call returns 0 or more, or minus a Winsock error code (-10035: would block). */

#ifndef __HALO_PS3_NET_H
#define __HALO_PS3_NET_H

#include <stdint.h>

/* type: 1 stream, 2 datagram (Winsock and lv2 number them alike) */
int ps3_net_socket(int type, int protocol);
int ps3_net_close(int socket);
int ps3_net_bind(int socket, uint32_t address, uint16_t port);
/* a connection under way on a socket that does not block gives Winsock's would-block */
int ps3_net_connect(int socket, uint32_t address, uint16_t port);
int ps3_net_listen(int socket, int backlog);
int ps3_net_accept(int socket, uint32_t *address, uint16_t *port);
int ps3_net_send(int socket, const void *buffer, uint32_t length, int flags);
int ps3_net_send_to(int socket, const void *buffer, uint32_t length, int flags, uint32_t address, uint16_t port);
int ps3_net_receive(int socket, void *buffer, uint32_t length, int flags);
int ps3_net_receive_from(int socket, void *buffer, uint32_t length, int flags, uint32_t *address, uint16_t *port);
int ps3_net_shutdown(int socket, int how);
int ps3_net_set_nonblocking(int socket, int nonblocking);
int ps3_net_set_nodelay(int socket);
/* level and name as lv2 has them: Winsock's SOL_SOCKET options have the same numbers */
int ps3_net_set_option(int socket, int level, int name, const void *value, uint32_t length);
int ps3_net_get_option(int socket, int level, int name, void *value, uint32_t *length);
int ps3_net_local_name(int socket, uint32_t *address, uint16_t *port);
int ps3_net_peer_name(int socket, uint32_t *address, uint16_t *port);
/* Winsock's select on three lists, each cut to its sockets that are ready; the count of them.
timeout_ms below 0 waits for ever */
int ps3_net_select(int *read, int *read_count, int *write, int *write_count, int *error, int *error_count,
	int timeout_ms);

/* this PS3's IPv4 address from netctl: 1 with it, 0 with none, -1 while netctl still gets one */
int ps3_net_local_address(uint32_t *address);
void ps3_net_random(void *buffer, uint32_t size);

#endif
