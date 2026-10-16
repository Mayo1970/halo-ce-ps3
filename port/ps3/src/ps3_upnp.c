/* posix.h's UPnP for internet play (p2p.c's thread alone calls it): the router forwards a UDP port to the tunnel.
posix_upnp.c's way, without miniupnpc, which selects on fd_set (PSL1GHT's descriptors have bit 30 set). */

#include "ps3_net.h"
#include "ps3_system.h"
#include "../../linux/src/posix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* (addresses and ports in network order, the PS3's own) */
enum
{
	/* milliseconds the router is looked for, and an exchange with it may take */
	DISCOVERY_MILLISECONDS = 2000,
	EXCHANGE_MILLISECONDS = 4000,
	/* a description or an answer (longer ones are cut) */
	MAXIMUM_RESPONSE = 0x10000,
	MAXIMUM_LOCATIONS = 4,
	LOCATION_SIZE = 256,
	/* ports tried when the router forwards the same one elsewhere, and its forwardings looked through for ones an
	earlier run left */
	PORT_ATTEMPTS = 8,
	MAXIMUM_CHECKED_FORWARDINGS = 64,
	WINSOCK_WOULD_BLOCK = 10035,
	SOCKET_STREAM = 1,
	SOCKET_DATAGRAM = 2,
};

#define SSDP_ADDRESS 0xEFFFFFFAu
#define SSDP_PORT 1900
#define FORWARDING_DESCRIPTION "Halo internet play"
#define LEASE_SECONDS "3600"

static struct
{
	int found;
	/* the router's connection service, where its actions go */
	uint32_t control_address;
	uint16_t control_port;
	char control_path[LOCATION_SIZE];
	char service_type[80];
	char lan_address[16];
	/* the router's answer: no leases but permanent ones */
	int permanent_only;
	char response[MAXIMUM_RESPONSE + 1];
} upnp;

/* ---------- text */

static void address_text(uint32_t address, char *text, size_t size)
{
	snprintf(text, size, "%u.%u.%u.%u", (unsigned)(address >> 24), (unsigned)(address >> 16 & 0xff),
		(unsigned)(address >> 8 & 0xff), (unsigned)(address & 0xff));
}

/* addr_is_reserved.c's ranges (miniupnpc): a router with such an internet address is behind another NAT */
static int address_reserved(uint32_t address)
{
	static const uint32_t ranges[][2] = {
		{ 0x00000000u, 8 }, { 0x0A000000u, 8 }, { 0x64400000u, 10 }, { 0x7F000000u, 8 }, { 0xA9FE0000u, 16 },
		{ 0xAC100000u, 12 }, { 0xC0000000u, 24 }, { 0xC0000200u, 24 }, { 0xC0A80000u, 16 }, { 0xC6120000u, 15 },
		{ 0xC6336400u, 24 }, { 0xCB007100u, 24 }, { 0xE0000000u, 4 }, { 0xF0000000u, 4 },
	};
	size_t index;

	for (index = 0; index < sizeof(ranges) / sizeof(ranges[0]); index++)
	{
		if ((address ^ ranges[index][0]) >> (32 - ranges[index][1]) == 0)
			return 1;
	}
	return 0;
}

/* the text of an element <tag>...</tag> in [text, end), into value: what follows the element, or NULL */
static const char *xml_element(const char *text, const char *end, const char *tag, char *value, size_t size)
{
	char open[64], close[64];
	const char *start, *stop;
	size_t length;

	snprintf(open, sizeof(open), "<%s>", tag);
	snprintf(close, sizeof(close), "</%s>", tag);
	start = strstr(text, open);
	if (!start || start >= end)
		return NULL;
	start += strlen(open);
	stop = strstr(start, close);
	if (!stop || stop > end)
		return NULL;
	length = (size_t)(stop - start);
	while (length && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n'))
		start++, length--;
	while (length && (start[length - 1] == ' ' || start[length - 1] == '\t' || start[length - 1] == '\r' ||
		start[length - 1] == '\n'))
	{
		length--;
	}
	if (length >= size)
		length = size - 1;
	memcpy(value, start, length);
	value[length] = 0;
	return stop + strlen(close);
}

/* http://host[:port][/path]: the host's address, the port and the path ("/" for none); 0 if it is no such URL */
static int url_parse(const char *url, uint32_t *address, uint16_t *port, char *path, size_t path_size)
{
	char host[128];
	const char *cursor = url;
	size_t length = 0;
	unsigned long number = 80;

	if (strncasecmp(cursor, "http://", 7))
		return 0;
	cursor += 7;
	while (*cursor && *cursor != ':' && *cursor != '/' && length + 1 < sizeof(host))
		host[length++] = *cursor++;
	host[length] = 0;
	if (*cursor == ':')
	{
		char *after;

		number = strtoul(cursor + 1, &after, 10);
		cursor = after;
	}
	if (!length || !number || number > 65535 || (*cursor && *cursor != '/'))
		return 0;
	*address = ps3_net_resolve(host);
	*port = (uint16_t)number;
	snprintf(path, path_size, "%s", *cursor ? cursor : "/");
	return *address != 0;
}

/* ---------- HTTP */

static int milliseconds_left(double deadline)
{
	double left = (deadline - ps3_time_seconds()) * 1000.0;

	return left > 0.0 ? (int)left + 1 : 0;
}

/* whether the answer so far is whole: its headers, and as much body as Content-Length says (or its last chunk) */
static int response_complete(int length)
{
	const char *headers_end = strstr(upnp.response, "\r\n\r\n");
	const char *field;

	if (!headers_end)
		return 0;
	for (field = upnp.response; field && field < headers_end; field = strstr(field, "\r\n"), field = field ? field + 2 : NULL)
	{
		if (!strncasecmp(field, "Content-Length:", 15))
			return length - (int)(headers_end + 4 - upnp.response) >= atoi(field + 15);
		if (!strncasecmp(field, "Transfer-Encoding:", 18) && strstr(field, "chunked") &&
			strstr(field, "chunked") < headers_end)
		{
			return strstr(headers_end, "\r\n0\r\n") != NULL;
		}
	}
	return 0;
}

/* an exchange with the router over a connection: the answer, ended, in upnp.response, and its length; -1 if none */
static int http_exchange(uint32_t address, uint16_t port, const char *request, int request_length)
{
	double deadline = ps3_time_seconds() + EXCHANGE_MILLISECONDS / 1000.0;
	int socket = ps3_net_socket(SOCKET_STREAM, 0);
	int length = 0;
	int sent = 0;
	int result;

	upnp.response[0] = 0;
	if (socket < 0)
		return -1;
	if (ps3_net_set_nonblocking(socket, 1) < 0)
		goto failed;
	result = ps3_net_connect(socket, address, port);
	if (result == -WINSOCK_WOULD_BLOCK)
	{
		int writable[1] = { socket }, failing[1] = { socket };
		int writable_count = 1, failing_count = 1;

		if (ps3_net_select(NULL, NULL, writable, &writable_count, failing, &failing_count,
				milliseconds_left(deadline)) <= 0 || !writable_count)
		{
			goto failed;
		}
	}
	else if (result < 0)
	{
		goto failed;
	}
	while (sent < request_length)
	{
		result = ps3_net_send(socket, request + sent, (uint32_t)(request_length - sent), 0);
		if (result == -WINSOCK_WOULD_BLOCK)
		{
			int writable[1] = { socket };
			int writable_count = 1;

			if (ps3_net_select(NULL, NULL, writable, &writable_count, NULL, NULL, milliseconds_left(deadline)) <= 0)
				goto failed;
			continue;
		}
		if (result <= 0)
			goto failed;
		sent += result;
	}
	while (length < MAXIMUM_RESPONSE && !response_complete(length))
	{
		result = ps3_net_receive(socket, upnp.response + length, (uint32_t)(MAXIMUM_RESPONSE - length), 0);
		if (result == -WINSOCK_WOULD_BLOCK)
		{
			int readable[1] = { socket };
			int readable_count = 1;

			if (ps3_net_select(readable, &readable_count, NULL, NULL, NULL, NULL, milliseconds_left(deadline)) <= 0)
				break;
			continue;
		}
		/* (0: the router closed the connection, the answer's end) */
		if (result <= 0)
			break;
		length += result;
		upnp.response[length] = 0;
	}
	ps3_net_close(socket);
	return length > 0 ? length : -1;

failed:
	ps3_net_close(socket);
	return -1;
}

/* the answer's status, and its body (in upnp.response, its chunks joined in place); NULL for none */
static char *http_body(int length, int *status)
{
	char *body = strstr(upnp.response, "\r\n\r\n");
	char *field;
	int chunked = 0;

	*status = 0;
	if (length <= 0 || !body || strncasecmp(upnp.response, "HTTP/1.", 7) || length < 12)
		return NULL;
	*status = atoi(upnp.response + 9);
	*body = 0;
	for (field = upnp.response; (field = strstr(field, "\r\n")) != NULL; )
	{
		field += 2;
		if (!strncasecmp(field, "Transfer-Encoding:", 18) && strstr(field, "chunked"))
			chunked = 1;
	}
	body += 4;
	if (chunked)
	{
		char *from = body;
		char *to = body;

		for (;;)
		{
			char *after;
			unsigned long size = strtoul(from, &after, 16);
			char *data = strstr(after, "\r\n");

			if (!size || !data || data + 2 + size > upnp.response + length)
				break;
			memmove(to, data + 2, size);
			to += size;
			from = data + 2 + size + 2;
			if (from > upnp.response + length)
				break;
		}
		*to = 0;
	}
	return body;
}

/* ---------- the router */

/* a SOAP action of the router's connection service with its arguments (elements): the HTTP status (0: no answer),
the answer's body in body */
static int soap_action(const char *action, const char *arguments, char **body)
{
	char envelope[1536];
	char request[2560];
	char host[16];
	int envelope_length, request_length, length, status;

	*body = NULL;
	envelope_length = snprintf(envelope, sizeof(envelope),
		"<?xml version=\"1.0\"?>\r\n"
		"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
		"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>"
		"<u:%s xmlns:u=\"%s\">%s</u:%s></s:Body></s:Envelope>\r\n",
		action, upnp.service_type, arguments, action);
	address_text(upnp.control_address, host, sizeof(host));
	request_length = snprintf(request, sizeof(request),
		"POST %s HTTP/1.1\r\nHost: %s:%u\r\nUser-Agent: PS3/1.0 UPnP/1.1 Halo/1.0\r\n"
		"Content-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"%s#%s\"\r\nContent-Length: %d\r\n"
		"Connection: close\r\n\r\n%s",
		upnp.control_path, host, (unsigned)upnp.control_port, upnp.service_type, action, envelope_length, envelope);
	if (envelope_length >= (int)sizeof(envelope) || request_length >= (int)sizeof(request))
		return 0;
	length = http_exchange(upnp.control_address, upnp.control_port, request, request_length);
	*body = http_body(length, &status);
	return *body ? status : 0;
}

/* an action's result: 0 when it succeeded, the UPnP error code (718: the port is taken) or the HTTP status */
static int action_result(int status, const char *body)
{
	char code[16];

	if (status == 200)
		return 0;
	if (status == 500 && body && xml_element(body, body + strlen(body), "errorCode", code, sizeof(code)))
		return atoi(code);
	return status ? status : -1;
}

static uint32_t router_internet_address(void)
{
	char *body;
	char text[32];
	int status = soap_action("GetExternalIPAddress", "", &body);

	if (status != 200 || !xml_element(body, body + strlen(body), "NewExternalIPAddress", text, sizeof(text)))
		return 0;
	return ps3_net_resolve(text);
}

/* the router's connection service in the description at location: 1 with it in upnp */
static int router_use_location(const char *location)
{
	static const char *const services[] = {
		"urn:schemas-upnp-org:service:WANIPConnection:2",
		"urn:schemas-upnp-org:service:WANIPConnection:1",
		"urn:schemas-upnp-org:service:WANPPPConnection:1",
	};
	char request[512], path[LOCATION_SIZE], base[LOCATION_SIZE], host[16];
	char type[80], control[LOCATION_SIZE];
	uint32_t address;
	uint16_t port;
	int request_length, status, length;
	char *body;
	const char *end;
	size_t service;

	if (!url_parse(location, &address, &port, path, sizeof(path)))
		return 0;
	address_text(address, host, sizeof(host));
	request_length = snprintf(request, sizeof(request),
		"GET %s HTTP/1.1\r\nHost: %s:%u\r\nUser-Agent: PS3/1.0 UPnP/1.1 Halo/1.0\r\nConnection: close\r\n\r\n",
		path, host, (unsigned)port);
	if (request_length >= (int)sizeof(request))
		return 0;
	length = http_exchange(address, port, request, request_length);
	body = http_body(length, &status);
	if (!body || status != 200)
		return 0;
	end = body + strlen(body);
	/* (relative control URLs are of URLBase, else of the description's host) */
	if (!xml_element(body, end, "URLBase", base, sizeof(base)) ||
		!url_parse(base, &upnp.control_address, &upnp.control_port, path, sizeof(path)))
	{
		upnp.control_address = address;
		upnp.control_port = port;
	}
	for (service = 0; service < sizeof(services) / sizeof(services[0]); service++)
	{
		const char *cursor;

		for (cursor = strstr(body, "<service>"); cursor; cursor = strstr(cursor + 1, "<service>"))
		{
			const char *close = strstr(cursor, "</service>");

			if (!close || !xml_element(cursor, close, "serviceType", type, sizeof(type)) || strcmp(type, services[service]) ||
				!xml_element(cursor, close, "controlURL", control, sizeof(control)))
			{
				continue;
			}
			snprintf(upnp.service_type, sizeof(upnp.service_type), "%s", type);
			if (!strncasecmp(control, "http://", 7))
			{
				if (!url_parse(control, &upnp.control_address, &upnp.control_port, upnp.control_path,
						sizeof(upnp.control_path)))
				{
					continue;
				}
			}
			else
			{
				snprintf(upnp.control_path, sizeof(upnp.control_path), "%s%s", control[0] == '/' ? "" : "/", control);
			}
			return 1;
		}
	}
	return 0;
}

/* the routers that answer an SSDP search on this network, their descriptions' URLs into locations: their count */
static int router_discover(char locations[][LOCATION_SIZE], int maximum)
{
	static const char *const targets[] = {
		"urn:schemas-upnp-org:device:InternetGatewayDevice:1",
		"urn:schemas-upnp-org:device:InternetGatewayDevice:2",
		"urn:schemas-upnp-org:service:WANIPConnection:1",
		"urn:schemas-upnp-org:service:WANPPPConnection:1",
	};
	double deadline = ps3_time_seconds() + DISCOVERY_MILLISECONDS / 1000.0;
	int socket = ps3_net_socket(SOCKET_DATAGRAM, 0);
	int count = 0;
	size_t target;

	if (socket < 0)
		return 0;
	ps3_net_set_nonblocking(socket, 1);
	for (target = 0; target < sizeof(targets) / sizeof(targets[0]); target++)
	{
		char search[256];
		int length = snprintf(search, sizeof(search),
			"M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: %s\r\n\r\n",
			targets[target]);

		ps3_net_send_to(socket, search, (uint32_t)length, 0, SSDP_ADDRESS, SSDP_PORT);
	}
	while (count < maximum)
	{
		char answer[1536];
		int readable[1] = { socket };
		int readable_count = 1;
		uint32_t from;
		uint16_t from_port;
		int length;
		const char *field;

		if (ps3_net_select(readable, &readable_count, NULL, NULL, NULL, NULL, milliseconds_left(deadline)) <= 0)
			break;
		length = ps3_net_receive_from(socket, answer, sizeof(answer) - 1, 0, &from, &from_port);
		if (length <= 0)
			continue;
		answer[length] = 0;
		for (field = answer; field; field = strstr(field, "\r\n"), field = field ? field + 2 : NULL)
		{
			char location[LOCATION_SIZE];
			size_t size = 0;
			int known = 0;
			int index;

			if (strncasecmp(field, "LOCATION:", 9))
				continue;
			field += 9;
			while (*field == ' ' || *field == '\t')
				field++;
			while (field[size] && field[size] != '\r' && field[size] != '\n' && size + 1 < sizeof(location))
				size++;
			memcpy(location, field, size);
			location[size] = 0;
			for (index = 0; index < count; index++)
				known |= !strcmp(locations[index], location);
			if (!known && size)
				snprintf(locations[count++], LOCATION_SIZE, "%s", location);
			break;
		}
	}
	ps3_net_close(socket);
	return count;
}

/* whether no socket here has this UDP port: a forwarding to it is one no run of the game uses now */
static int port_unused(uint16_t port)
{
	int socket = ps3_net_socket(SOCKET_DATAGRAM, 0);
	int unused;

	if (socket < 0)
		return 0;
	unused = ps3_net_bind(socket, 0, port) == 0;
	ps3_net_close(socket);
	return unused;
}

/* the forwardings to this machine that an earlier run left (it crashed, or the router answered too slowly) */
static void router_remove_left_forwardings(void)
{
	int index = 0;
	int checked;

	for (checked = 0; checked < MAXIMUM_CHECKED_FORWARDINGS; checked++)
	{
		char arguments[128], description[96], client[64], protocol[8], internal[8], external[8];
		char *body;
		const char *end;
		int status;
		unsigned long port;

		snprintf(arguments, sizeof(arguments), "<NewPortMappingIndex>%d</NewPortMappingIndex>", index);
		status = soap_action("GetGenericPortMappingEntry", arguments, &body);
		/* (713 SpecifiedArrayIndexInvalid: the last was the end) */
		if (status != 200)
			break;
		end = body + strlen(body);
		if (xml_element(body, end, "NewPortMappingDescription", description, sizeof(description)) &&
			xml_element(body, end, "NewInternalClient", client, sizeof(client)) &&
			xml_element(body, end, "NewProtocol", protocol, sizeof(protocol)) &&
			xml_element(body, end, "NewInternalPort", internal, sizeof(internal)) &&
			xml_element(body, end, "NewExternalPort", external, sizeof(external)) &&
			!strcmp(description, FORWARDING_DESCRIPTION) && !strcmp(protocol, "UDP") &&
			!strcmp(client, upnp.lan_address) && (port = strtoul(internal, NULL, 10)) > 0 && port < 65536 &&
			port_unused((uint16_t)port))
		{
			char remove[256];

			snprintf(remove, sizeof(remove), "<NewRemoteHost></NewRemoteHost><NewExternalPort>%s</NewExternalPort>"
				"<NewProtocol>UDP</NewProtocol>", external);
			/* (the next one has this index now) */
			if (action_result(soap_action("DeletePortMapping", remove, &body), body) == 0)
				continue;
		}
		index++;
	}
}

/* the router, looked for until it is found (the PS3 may have moved to another network since); 1 if there is one */
static int router_find(char *error, int error_size)
{
	char locations[MAXIMUM_LOCATIONS][LOCATION_SIZE];
	uint32_t lan_address = 0;
	int count, index;
	int private_only = 0;

	if (upnp.found)
		return 1;
	if (ps3_net_local_address(&lan_address) != 1)
	{
		snprintf(error, (size_t)error_size, "this PS3 has no address on its network");
		return 0;
	}
	address_text(lan_address, upnp.lan_address, sizeof(upnp.lan_address));
	count = router_discover(locations, MAXIMUM_LOCATIONS);
	for (index = 0; index < count; index++)
	{
		uint32_t internet_address;

		if (!router_use_location(locations[index]))
			continue;
		internet_address = router_internet_address();
		if (!internet_address)
			continue;
		if (address_reserved(internet_address))
		{
			private_only = 1;
			continue;
		}
		upnp.found = 1;
		ps3_log_printf("[net] UPnP router %s (%s)\n", locations[index], upnp.service_type);
		router_remove_left_forwardings();
		return 1;
	}
	snprintf(error, (size_t)error_size, private_only ?
		"the router's internet address is a private one: another NAT is beyond it" : "no UPnP router on this network");
	return 0;
}

static int router_add(unsigned int external_port, unsigned int internal_port)
{
	char arguments[512];
	char *body;
	int result;

	snprintf(arguments, sizeof(arguments),
		"<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort><NewProtocol>UDP</NewProtocol>"
		"<NewInternalPort>%u</NewInternalPort><NewInternalClient>%s</NewInternalClient><NewEnabled>1</NewEnabled>"
		"<NewPortMappingDescription>" FORWARDING_DESCRIPTION "</NewPortMappingDescription>"
		"<NewLeaseDuration>%s</NewLeaseDuration>",
		external_port, internal_port, upnp.lan_address, upnp.permanent_only ? "0" : LEASE_SECONDS);
	result = action_result(soap_action("AddPortMapping", arguments, &body), body);
	/* 725 OnlyPermanentLeasesSupported */
	if (result == 725 && !upnp.permanent_only)
	{
		upnp.permanent_only = 1;
		return router_add(external_port, internal_port);
	}
	return result;
}

int posix_upnp_forward_udp(unsigned short port, unsigned short preferred_port, posix_ulong *external_address,
	unsigned short *external_port, char *error, int error_size)
{
	unsigned int try_port = preferred_port ? preferred_port : port;
	uint32_t internet_address;
	int attempt;
	int result = 0;

	if (!router_find(error, error_size))
		return 0;
	internet_address = router_internet_address();
	if (!internet_address)
	{
		snprintf(error, (size_t)error_size, "the router did not say its internet address");
		return 0;
	}
	for (attempt = 0; attempt < PORT_ATTEMPTS; attempt++)
	{
		uint16_t random;

		result = router_add(try_port, port);
		/* 718 ConflictInMappingEntry: another machine has that port; 724 SamePortValuesRequired: this one only */
		if (result != 718 || attempt + 1 == PORT_ATTEMPTS)
			break;
		ps3_net_random(&random, sizeof(random));
		try_port = 49152u + random % 16384u;
	}
	if (result)
	{
		snprintf(error, (size_t)error_size, "the router refused to forward a port (%d)", result);
		return 0;
	}
	*external_address = (posix_ulong)internet_address;
	*external_port = (unsigned short)try_port;
	return 1;
}

void posix_upnp_stop_forwarding_udp(unsigned short external_port)
{
	char arguments[256];
	char *body;

	if (!upnp.found)
		return;
	snprintf(arguments, sizeof(arguments), "<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort>"
		"<NewProtocol>UDP</NewProtocol>", (unsigned)external_port);
	soap_action("DeletePortMapping", arguments, &body);
}
