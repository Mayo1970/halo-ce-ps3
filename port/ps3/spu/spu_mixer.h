/* SPU_MIXER.H: the DirectSound streams' decode and mix on an SPU (spu_mixer.c), one chunk a job. The SPU, the host
layer (ps3_spu.c) and dsound_ps3.c share it: fixed-width types only, so the layouts are the same in all three. */

#ifndef __HALO_PS3_SPU_MIXER_H
#define __HALO_PS3_SPU_MIXER_H

#include <stdint.h>

/* a chunk: one audio port block of stereo frames */
#define SPU_MIXER_FRAMES 256
/* the game's sound channels (sound_dsound_xbox.c), a stream each */
#define SPU_MIXER_VOICES 256
/* the packets a pass takes from a stream (dsound_ps3.c's MIX_PACKETS) */
#define SPU_MIXER_PACKETS 8
/* the SPU thread's event port for a finished job */
#define SPU_MIXER_EVENT_PORT 16

#define SPU_MIXER_RUNNING 1
#define SPU_MIXER_EXITED 2

/* spu_mixer_packet.flags */
#define SPU_MIXER_DECODED 1u
#define SPU_MIXER_FINISHED 2u

/* a packet as the pass took it: the game's bytes and their floats, 128-aligned with the size rounded up to 128
bytes (the SPU's DMA writes whole 16-byte lines) */
typedef struct
{
	uint32_t source;
	uint32_t size;
	uint32_t samples;
	uint32_t frames;
	uint32_t flags;
	uint32_t pad[3];
} spu_mixer_packet;

/* a stream's format, its position in its first packet, and the chunk's step and gains (dsound_ps3.c's mix_begin) */
typedef struct
{
	uint32_t channels;
	uint32_t adpcm;
	uint32_t packet_count;
	uint32_t index;
	float fraction;
	uint32_t step_whole;
	float step_fraction;
	float left, right;
	float ramp_left, ramp_right;
	/* the packets the job decoded */
	uint32_t decoded;
	uint32_t pad[4];
	spu_mixer_packet packets[SPU_MIXER_PACKETS];
} spu_mixer_voice;

typedef struct
{
	uint32_t sequence;
	uint32_t voice_count;
	uint32_t frames;
	uint32_t pad[29];
} spu_mixer_header;

/* the voices in the PPE's order, so the chunk's sums are its own */
typedef struct
{
	spu_mixer_header header;
	float output[SPU_MIXER_FRAMES * 2];
	spu_mixer_voice voices[SPU_MIXER_VOICES];
} __attribute__((aligned(128))) spu_mixer_job;

/* one layout for spu-gcc and the PPE's two ABIs */
typedef char spu_mixer_layout_check[sizeof(spu_mixer_voice) == 320 && sizeof(spu_mixer_job) == 84096 ? 1 : -1];

/* the PPE writes it; its store ends the SPU's reservation of the line, which wakes the SPU */
typedef struct
{
	volatile uint32_t sequence;
	volatile uint32_t quit;
	uint32_t pad[30];
} __attribute__((aligned(128))) spu_mixer_bell;

/* the SPU puts it after each job, behind the results; the PPE only reads it */
typedef struct
{
	volatile uint32_t state;
	volatile uint32_t sequence;
	volatile uint32_t jobs;
	/* timebase ticks (the SPU's decrementer) in the jobs and in their decodes */
	volatile uint32_t busy_ticks;
	volatile uint32_t decode_ticks;
	volatile uint32_t packets_decoded;
	/* the wakes by the bell and by the 1 ms decrementer; the events lv2 refused */
	volatile uint32_t wakes;
	volatile uint32_t timeouts;
	volatile uint32_t event_errors;
	/* where the SPU's own copy is, for the PPE's read when a job does not end */
	volatile uint32_t ls_address;
	volatile uint32_t stage;
	uint32_t pad[21];
} __attribute__((aligned(128))) spu_mixer_echo;

#endif
