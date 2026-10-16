/* Mbed TLS on the PS3 (MBEDTLS_USER_CONFIG_FILE, after its own configuration): none of Unix's entropy, clock,
sockets or files; lv2's random numbers and the timebase instead (port/ps3/src/ps3_mbedtls.c) */

#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT
#define MBEDTLS_PLATFORM_MS_TIME_ALT
#undef MBEDTLS_TIMING_C
#undef MBEDTLS_NET_C
#undef MBEDTLS_FS_IO
#undef MBEDTLS_PSA_ITS_FILE_C
#undef MBEDTLS_PSA_CRYPTO_STORAGE_C
