/* The PS3's invite codes (p2p_crypto.c's HALO_PS3 block) on the host, for tools/ps3_invite_code_test.py: a line
"<digits> <payload hex or ->" gives the key's ms, key, topic and invite opened; "seal <digits> <invite>", the seal. */

#define HALO_PS3 1
/* p2p_crypto.c without the platform layer, whose two calls in it are stubs here */
#define __HALO_LINUX_PLATFORM_H
#define __HALO_LINUX_POSIX_H
void posix_random_bytes(void *buffer, unsigned int size);
void platform_log(const char *format, ...);

#include "p2p_crypto.c"

#include <stdio.h>
#include <time.h>

/* (the seal's nonce: random enough for a test) */
void posix_random_bytes(void *buffer, unsigned int size)
{
	unsigned char *bytes = buffer;
	unsigned int index;

	for (index = 0; index < size; index++)
		bytes[index] = (unsigned char)(rand() >> 7);
}

void platform_log(const char *format, ...)
{
	(void)format;
}

void p2p_hex(const unsigned char *bytes, int size, char *text)
{
	int index;

	for (index = 0; index < size; index++)
		sprintf(text + index * 2, "%02x", bytes[index]);
	text[size * 2] = 0;
}

static int from_hex(const char *hex, unsigned char *bytes, int maximum)
{
	int size = 0;
	unsigned int byte;

	while (strcmp(hex, "-") && size < maximum && hex[size * 2] && hex[size * 2 + 1] &&
		sscanf(hex + size * 2, "%2x", &byte) == 1)
	{
		bytes[size++] = (unsigned char)byte;
	}
	return size;
}

int main(void)
{
	unsigned char key[P2P_SHA256_SIZE], payload[256], invite[P2P_CODE_INVITE_SIZE];
	char line[1024], first[64], digits[64], hex[600], last[64] = "", topic[P2P_CODE_TOPIC_SIZE];
	char text[2 * P2P_CODE_PAYLOAD_SIZE + 1];
	long milliseconds = 0;
	int sealing;

	srand((unsigned int)time(NULL));
	while (fgets(line, sizeof(line), stdin))
	{
		int size;

		sealing = sscanf(line, "%63s %63s %599s", first, digits, hex) == 3 && !strcmp(first, "seal");
		if (!sealing && sscanf(line, "%63s %599s", digits, hex) != 2)
			continue;
		/* (the key once for a run of lines of one code) */
		if (strcmp(digits, last))
		{
			clock_t start = clock();

			p2p_code_key(digits, key);
			milliseconds = (long)((clock() - start) * 1000 / CLOCKS_PER_SEC);
			strcpy(last, digits);
		}
		p2p_hex(key, sizeof(key), text);
		p2p_code_topic(key, topic);
		printf("%ld %s %s ", milliseconds, text, topic);
		if (sealing)
		{
			from_hex(hex, invite, sizeof(invite));
			p2p_code_seal(key, invite, payload);
			p2p_hex(payload, P2P_CODE_PAYLOAD_SIZE, text);
			printf("%s\n", text);
			continue;
		}
		size = from_hex(hex, payload, sizeof(payload));
		if (p2p_code_open(key, payload, size, invite))
		{
			p2p_hex(invite, sizeof(invite), text);
			printf("%s\n", text);
		}
		else
			printf("none\n");
	}
	return 0;
}
