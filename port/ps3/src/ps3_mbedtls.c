/* What Mbed TLS asks of the platform on the PS3 (port/ps3/include/ps3_mbedtls_config.h): its entropy from lv2's
random numbers, and its clock of milliseconds. */

#include "ps3_host.h"
#include "ps3_net.h"

#include "mbedtls/entropy.h"
#include "mbedtls/platform_time.h"

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t length, size_t *output_length)
{
	(void)data;
	*output_length = 0;
	if (!ps3_net_secure_random(output, (uint32_t)length))
		return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
	*output_length = length;
	return 0;
}

mbedtls_ms_time_t mbedtls_ms_time(void)
{
	return (mbedtls_ms_time_t)(ps3_host_microseconds() / 1000);
}
