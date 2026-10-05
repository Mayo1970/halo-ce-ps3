/* TRANSPORT_ADDRESS.C. Its Xbox symbols and their addresses are listed in the
upstream halo-ce-universal copy of source/bungie_net/network/transport_address.c. */

/* ---------- headers */

#include "cseries/cseries.h"

#include "bungie_net/network/transport.h"

/* ---------- constants */

/* ---------- macros */

/* ---------- structures */

/* ---------- prototypes */

/* ---------- globals */

#ifndef HALO_ANDROID /* Mach-O section names differ; the default is .bss anyway */
#pragma bss_seg(".bss")
#endif
static char transport_address_string[256];
#ifndef HALO_ANDROID
#pragma bss_seg()
#endif

/* ---------- public code */

struct transport_address *create_transport_address(
	struct transport_address_data const *address,
	word address_length,
	word port)
{
	struct transport_address *result;

	match_assert("c:\\halo\\SOURCE\\bungie_net\\network\\transport_address.c", 30, transport_initialized);
	match_assert("c:\\halo\\SOURCE\\bungie_net\\network\\transport_address.c", 31, address);

	result = match_malloc(
		"c:\\halo\\SOURCE\\bungie_net\\network\\transport_address.c",
		33,
		sizeof(*result));
	if (result)
	{
		result->address = *address;
		result->address_length = address_length;
		result->port = port;
		result->address_type = 0;
	}

	return result;
}

void delete_transport_address(
	struct transport_address *address)
{
	match_assert("c:\\halo\\SOURCE\\bungie_net\\network\\transport_address.c", 47, transport_initialized);
	match_assert("c:\\halo\\SOURCE\\bungie_net\\network\\transport_address.c", 48, address);
	match_free("c:\\halo\\SOURCE\\bungie_net\\network\\transport_address.c", 50, address);
	return;
}

long transport_address_equivalent(
	struct transport_address const *a,
	struct transport_address const *b)
{
	match_assert("c:\\halo\\SOURCE\\bungie_net\\network\\transport_address.c", 59, a);
	match_assert("c:\\halo\\SOURCE\\bungie_net\\network\\transport_address.c", 60, b);
	match_assert("c:\\halo\\SOURCE\\bungie_net\\network\\transport_address.c", 61, transport_initialized);
	match_assert("c:\\halo\\SOURCE\\bungie_net\\network\\transport_address.c", 63, IPV4_ADDRESS_LENGTH == a->address_length);
	match_assert("c:\\halo\\SOURCE\\bungie_net\\network\\transport_address.c", 64, IPV4_ADDRESS_LENGTH == b->address_length);

	if (csmemcmp(
			&a->address,
			&b->address,
			a->address_length > b->address_length ? a->address_length : b->address_length) == 0 &&
		a->port == b->port)
		return TRUE;

	return FALSE;
}

char const *transport_address_to_string(
	struct transport_address const *addr)
{
	match_assert("c:\\halo\\SOURCE\\bungie_net\\network\\transport_address.c", 74, addr);
	match_assert("c:\\halo\\SOURCE\\bungie_net\\network\\transport_address.c", 75, IPV4_ADDRESS_LENGTH == addr->address_length);

	transport_address_string[0] = 0;
	if (addr->address_length == IPV4_ADDRESS_LENGTH)
	{
		_snprintf(
			transport_address_string,
			NUMBEROF(transport_address_string),
			"%hd.%hd.%hd.%hd:%hd",
#ifdef __BIG_ENDIAN__
			/* port: the host-order long's most significant byte comes first in memory here */
			addr->address.bytes[0],
			addr->address.bytes[1],
			addr->address.bytes[2],
			addr->address.bytes[3],
#else
			addr->address.bytes[3],
			addr->address.bytes[2],
			addr->address.bytes[1],
			addr->address.bytes[0],
#endif
			addr->port);
	}
	else if (addr->address_length == IPV6_ADDRESS_LENGTH)
	{
		_snprintf(
			transport_address_string,
			NUMBEROF(transport_address_string),
			"%4X.%4X.%4X.%4X.%4X.%4X.%4X.%4X:%hd",
			addr->address.words[0],
			addr->address.words[1],
			addr->address.words[2],
			addr->address.words[3],
			addr->address.words[4],
			addr->address.words[5],
			addr->address.words[6],
			addr->address.words[7],
			addr->port);
	}

	return transport_address_string;
}

char const *transport_error_to_string(
	short error)
{
	switch (error)
	{
	case _transport_error_none:
		return "_transport_error_none";
	case _transport_error_unknown:
		return "_transport_error_unknown";
	case _transport_error_endpoint_io:
		return "_transport_error_endpoint_io";
	case _transport_error_connection_lost:
		return "_transport_error_connection_lost";
	case _transport_result_operation_would_block:
		return "_transport_result_operation_would_block";
	case _transport_error_not_initialized:
		return "_transport_error_not_initialized";
	case _transport_result_already_initialized:
		return "_transport_result_already_initialized";
	case _transport_error_bad_input_parameters:
		return "_transport_error_bad_input_parameters";
	case _transport_error_dns_lookup_failure:
		return "_transport_error_dns_lookup_failure";
	case _transport_error_out_of_memory:
		return "_transport_error_out_of_memory";
	case _transport_error_seg_fault:
		return "_transport_error_seg_fault";
	case _transport_error_buffers_full:
		return "_transport_error_buffers_full";
	case _transport_error_bad_endpoint:
		return "_transport_error_bad_endpoint";
	case _transport_result_poll_timeout:
		return "_transport_result_poll_timeout";
	case _transport_error_bind_endpoint:
		return "_transport_error_bind_endpoint";
	case _transport_error_address_unknown:
		return "_transport_error_address_unknown";
	case _transport_error_connect_failed:
		return "_transport_error_connect_failed";
	case _transport_error_listen_failed:
		return "_transport_error_listen_failed";
	case _transport_error_options_failed:
		return "_transport_error_options_failed";
	case _transport_error_endpoint_not_in_set:
		return "_transport_error_endpoint_not_in_set";
	case _transport_error_endpoint_set_full:
		return "_transport_error_endpoint_set_full";
	case _transport_error_poll_error:
		return "_transport_error_poll_error";
	case _transport_result_dns_lookup_in_progress:
		return "_transport_result_dns_lookup_in_progress";
	case _transport_result_connect_in_progress:
		return "_transport_result_connect_in_progress";
	default:
		return "<unknown transport error>";
	}
}

/* ---------- private code */
