/* DirectSound on the PS3 audio port (src/ps3_audio.c), after port/linux/src/dsound_sdl.c: a mixer thread keeps the
port's ring 43 ms ahead, an SPU decodes and mixes (ps3_spu.c); packets complete on the game's thread (DoWork, Flush). */

#include "ps3_audio.h"
#include "ps3_spu.h"
#include "ps3_timers.h"
#include "port_config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OUTPUT_RATE PS3_AUDIO_RATE
#define OUTPUT_CHANNELS 2
#define MAXIMUM_STREAM_PACKETS 64
/* the packets a pass takes from a stream: a chunk spans a few at most (the game queues 4) */
#define MIX_PACKETS 8
/* buffers a flush takes from the mixer's pass: the game's 256 channels of 4 packets */
#define RETIRED_MAXIMUM 1024
/* the packets' buffers by size, 16, 32 and 64 KB (a packet: up to 8,192 frames of 2 channels); a first block of 4 MB,
then 1 MB more each time those are used up */
#define SAMPLE_CLASSES 3
#define SAMPLE_CLASS_BYTES 16384
#define SAMPLE_FIRST_BLOCK_BYTES (4 * 1024 * 1024)
#define SAMPLE_BLOCK_BYTES (1024 * 1024)
/* one audio port block */
#define MIX_CHUNK_FRAMES 256
#define MIX_CHUNK_MICROSECONDS (MIX_CHUNK_FRAMES * 1000000 / OUTPUT_RATE)
/* 43 ms in the ring: a mixer pass that waits behind other threads does not starve the port */
#define QUEUE_TARGET_FRAMES (8 * MIX_CHUNK_FRAMES)
#define MIX_SLEEP_MICROSECONDS 4000
#define MIXER_STACK_SIZE 0x10000
#define STATISTICS_MICROSECONDS 10000000

#define XBOX_ADPCM_BLOCK_BYTES 36
#define XBOX_ADPCM_BLOCK_SAMPLES 64
#define IMA_STEP_COUNT 89

/* ---------- voices */

/* what streams and buffers share: the source format, the gains, the 3D settings and the read position */
struct mixer_voice
{
	BOOL adpcm;
	unsigned long channels;
	DWORD sample_rate;
	DWORD frequency;
	float volume;
	float mix_left, mix_right;
	BOOL has_3d;
	DWORD mode;
	float position[3];
	float minimum_distance, maximum_distance;
	float i3dl2_gain;
	/* in source frames: a whole frame and a fraction, so the mixer converts no float to an integer */
	unsigned long index;
	float fraction;
	/* the last chunk's gains, which the next chunk ramps from against clicks */
	float current_left, current_right;
	BOOL gains_valid;
};

struct voice_packet
{
	XMEDIAPACKET packet;
	/* the game's data, which stays until the packet completes, as the Xbox's hardware reads it in place */
	const unsigned char *source;
	unsigned long size;
	/* taken at Process (samples_take), decoded by the mixer: interleaved floats, the source's channel count; the
	buffer's bytes */
	float *samples;
	unsigned long frames;
	unsigned long bytes;
	/* the mixer's, set with the lock held: decoded; played out, not yet completed */
	BOOL decoded;
	BOOL finished;
};

/* the per-chunk resampling step and gain ramp of one voice */
struct mix_state
{
	unsigned long step_whole;
	float step_fraction;
	float left, right;
	float ramp_left, ramp_right;
};

/* a packet as a mixer pass took it: a copy, as a flush during the pass can empty and refill the slot */
struct mix_packet
{
	struct voice_packet *slot;
	const unsigned char *source;
	unsigned long size;
	float *samples;
	unsigned long frames;
	BOOL decoded;
	BOOL finished;
};

struct ps3_stream
{
	/* first: in C an IDirectSoundStream is just { lpVtbl } */
	IDirectSoundStream object;
	struct ps3_stream *next;
	ULONG reference_count;
	LPFNXMEDIAOBJECTCALLBACK callback;
	LPVOID context;
	/* the play state (index, fraction, gains) is the mixer's: the game's thread asks for a restart */
	struct mixer_voice voice;
	struct voice_packet packets[MAXIMUM_STREAM_PACKETS];
	unsigned long packet_head;
	unsigned long packet_count;
	/* with the lock: the flushes, which void a pass's results; a start from the first frame */
	unsigned long generation;
	BOOL restart;
	/* the mixer's pass: the next stream, the generation, the packets it took, its step and gains */
	struct ps3_stream *mix_next;
	unsigned long mix_generation;
	unsigned long mix_count;
	struct mix_packet mix_packets[MIX_PACKETS];
	struct mix_state mix_state;
};

/* the inanity buffer (dsound_fix_rear_speakers) and the loading screen's tones (progress_bar.c) */
struct ps3_buffer
{
	struct ps3_buffer *next;
	ULONG reference_count;
	struct mixer_voice voice;
	float *samples;
	unsigned long frames;
	unsigned long loop_start, loop_end;
	BOOL playing;
	BOOL looping;
};

typedef char spu_mixer_size_check[MIX_PACKETS == SPU_MIXER_PACKETS && MIX_CHUNK_FRAMES == SPU_MIXER_FRAMES ? 1 : -1];

static struct ps3_host_lock *mixer_lock;
static struct ps3_stream *streams;
static struct ps3_buffer *buffers;

/* the listener, in DirectSound's left-handed +y up space */
static struct
{
	float position[3];
	float front[3];
	float top[3];
	float rolloff_factor;
	float distance_factor;
} listener = { { 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 }, 1.0f, 1.0f };

static float master_volume = 1.0f;
static BOOL audio_started;
static int audio_port;

/* written by the mixer thread, read for the log */
static volatile unsigned long mixer_chunks;
static volatile unsigned long mixer_microseconds;
static volatile unsigned long mixer_maximum_microseconds;
static volatile unsigned long mixer_decode_microseconds;
static volatile unsigned long mixer_hold_microseconds;
static volatile unsigned long mixer_hold_maximum_microseconds;
static volatile unsigned long packets_decoded;
static uint64_t mixer_hold_start;

/* odd while a pass mixes without the lock, between its two holds; the mixer counts it with the lock held */
static volatile unsigned long mixer_pass;
/* the packets the mixer finished (with the lock), and that count when the game's thread last completed them */
static volatile unsigned long packets_finished;
static unsigned long packets_completed;

/* buffers a flush took from a pass, given back once that pass is over (game thread, with the lock) */
static struct
{
	float *samples;
	unsigned long bytes;
	unsigned long pass;
} retired[RETIRED_MAXIMUM];
static unsigned long retired_count;

/* the packets' buffers, from blocks never freed: the heap's 10 to 25 allocations and frees a second fragmented it
(ps3_heap.c's log). A free buffer's first bytes link the next free one of its size. Game thread, with the lock */
static struct
{
	void *free[SAMPLE_CLASSES];
	unsigned char *block;
	unsigned long room;
	unsigned long blocks, block_bytes;
	unsigned long in_use, peak;
} samples_store;

/* the game's thread (DirectSoundCreate comes first, on the main thread); its waits are timed */
static void mixer_enter(void)
{
	uint64_t start;

	if (!mixer_lock)
		mixer_lock = ps3_host_lock_create(FALSE);
	/* uncontended, without the wait records of ps3_host_lock_enter */
	if (ps3_host_lock_try_enter(mixer_lock))
		return;
	start = ps3_timer_now();
	ps3_host_lock_enter(mixer_lock);
	ps3_timer_add(_ps3_timer_sound_lock_wait, start);
}

static void mixer_leave(void)
{
	ps3_host_lock_leave(mixer_lock);
}

/* the mixer thread's holds of the lock, timed */
static void mixer_hold_begin(void)
{
	if (!ps3_host_lock_try_enter(mixer_lock))
		ps3_host_lock_enter(mixer_lock);
	mixer_hold_start = ps3_host_microseconds();
}

static void mixer_hold_end(void)
{
	unsigned long held = (unsigned long)(ps3_host_microseconds() - mixer_hold_start);

	ps3_host_lock_leave(mixer_lock);
	mixer_hold_microseconds += held;
	if (held > mixer_hold_maximum_microseconds)
		mixer_hold_maximum_microseconds = held;
}

/* (game thread, without the lock) waits up to 100 ms for the end of pass, an odd count; the count is read with
the lock, so the pass's reads come before what the caller frees */
static void mixer_wait_pass(unsigned long pass)
{
	int tries;

	for (tries = 0; tries < 200; tries++)
	{
		unsigned long now;

		mixer_enter();
		now = mixer_pass;
		mixer_leave();
		if (now != pass)
			return;
		ps3_host_sleep_microseconds(500);
	}
}

/* the size class of a buffer's bytes; SAMPLE_CLASSES: over the largest */
static int samples_class(unsigned long bytes)
{
	int class = 0;

	while (class < SAMPLE_CLASSES && (unsigned long)SAMPLE_CLASS_BYTES << class < bytes)
		class++;
	return class;
}

static void samples_free_push(int class, void *buffer)
{
	*(void **)buffer = samples_store.free[class];
	samples_store.free[class] = buffer;
}

/* (game thread, with the lock) a new block; the last one's rest, a multiple of 16 KB, goes to the smaller sizes */
static BOOL samples_block_add(void)
{
	unsigned long bytes = samples_store.blocks ? SAMPLE_BLOCK_BYTES : SAMPLE_FIRST_BLOCK_BYTES;
	unsigned char *block;
	int class;

	for (class = SAMPLE_CLASSES - 1; class >= 0; class--)
	{
		unsigned long size = (unsigned long)SAMPLE_CLASS_BYTES << class;

		while (samples_store.room >= size)
		{
			samples_free_push(class, samples_store.block);
			samples_store.block += size;
			samples_store.room -= size;
		}
	}
	block = ps3_spu_buffer_allocate(bytes);
	if (!block)
		return FALSE;
	samples_store.block = block;
	samples_store.room = bytes;
	samples_store.blocks++;
	samples_store.block_bytes += bytes;
	return TRUE;
}

/* (game thread, with the lock) a buffer no pass reads, back to its size's free list (one over 64 KB: the heap's) */
static void samples_give_back(float *samples, unsigned long bytes)
{
	int class = samples_class(bytes);

	if (class == SAMPLE_CLASSES)
	{
		free(samples);
		return;
	}
	samples_store.in_use -= bytes;
	samples_free_push(class, samples);
}

/* (game thread, with the lock) a buffer of the smallest size that holds bytes, its bytes in *taken; NULL: no memory */
static float *samples_take(unsigned long bytes, unsigned long *taken)
{
	int class = samples_class(bytes);
	unsigned long size = (unsigned long)SAMPLE_CLASS_BYTES << class;
	void *buffer;

	if (class == SAMPLE_CLASSES)
	{
		/* (none of the game's packets) */
		*taken = bytes;
		return ps3_spu_buffer_allocate(bytes);
	}
	buffer = samples_store.free[class];
	if (buffer)
		samples_store.free[class] = *(void **)buffer;
	else
	{
		if (samples_store.room < size && !samples_block_add())
			return NULL;
		buffer = samples_store.block;
		samples_store.block += size;
		samples_store.room -= size;
	}
	*taken = size;
	samples_store.in_use += size;
	if (samples_store.in_use > samples_store.peak)
		samples_store.peak = samples_store.in_use;
	return buffer;
}

/* (game thread, with the lock) gives back the retired buffers whose pass is over */
static void retired_free(void)
{
	unsigned long index, kept = 0;

	for (index = 0; index < retired_count; index++)
	{
		if (retired[index].pass == mixer_pass)
			retired[kept++] = retired[index];
		else
			samples_give_back(retired[index].samples, retired[index].bytes);
	}
	retired_count = kept;
}

/* (game thread, with the lock) a completed packet's buffer; the mixer's pass may read an unfinished one */
static void samples_release(float *samples, unsigned long bytes, BOOL finished)
{
	if (!samples)
		return;
	retired_free();
	if (finished || !(mixer_pass & 1))
	{
		samples_give_back(samples, bytes);
		return;
	}
	if (retired_count == RETIRED_MAXIMUM)
	{
		unsigned long pass = mixer_pass;

		/* (full: the pass's end, which needs the lock; a later pass cannot take this completed packet) */
		mixer_leave();
		mixer_wait_pass(pass);
		mixer_enter();
		retired_free();
		if (mixer_pass != pass)
			samples_give_back(samples, bytes);
		else
			platform_log("DirectSound: a flushed packet's buffer kept (the mixer's pass did not end)");
		return;
	}
	retired[retired_count].samples = samples;
	retired[retired_count].bytes = bytes;
	retired[retired_count].pass = mixer_pass;
	retired_count++;
}

static float gain_from_millibels(LONG millibels)
{
	if (millibels <= DSBVOLUME_MIN)
		return 0.0f;
	return powf(10.0f, (float)millibels / 2000.0f);
}

static void voice_initialize(struct mixer_voice *voice, const WAVEFORMATEX *format, BOOL has_3d)
{
	voice->adpcm = format && format->wFormatTag == WAVE_FORMAT_XBOX_ADPCM;
	voice->channels = format && format->nChannels == 2 ? 2 : 1;
	voice->sample_rate = format ? format->nSamplesPerSec : 0;
	voice->frequency = voice->sample_rate;
	voice->volume = 1.0f;
	/* DirectSound's default mix bins: a mono voice to both fronts, a stereo voice's channels to left and right */
	voice->mix_left = 1.0f;
	voice->mix_right = 1.0f;
	voice->has_3d = has_3d;
	voice->mode = DS3DMODE_NORMAL;
	voice->minimum_distance = DS3D_DEFAULTMINDISTANCE;
	voice->maximum_distance = DS3D_DEFAULTMAXDISTANCE;
	voice->i3dl2_gain = 1.0f;
}

/* ---------- decoding */

static const int ima_index_table[16] =
{
	-1, -1, -1, -1, 2, 4, 6, 8,
	-1, -1, -1, -1, 2, 4, 6, 8,
};

static const int ima_step_table[IMA_STEP_COUNT] =
{
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
	50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
	253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
	1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
	3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
	11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
	32767,
};

/* IMA's difference for each step and the nibble's magnitude bits, as floats: the PPE converts an integer to a
float through memory, a stall per sample; the sums stay whole numbers, so the result is IMA's, bit for bit */
static float adpcm_differences[IMA_STEP_COUNT][8];

static void adpcm_tables_initialize(void)
{
	int index, bits;

	for (index = 0; index < IMA_STEP_COUNT; index++)
	{
		for (bits = 0; bits < 8; bits++)
		{
			int step = ima_step_table[index];
			int difference = step >> 3;

			if (bits & 1)
				difference += step >> 2;
			if (bits & 2)
				difference += step >> 1;
			if (bits & 4)
				difference += step;
			adpcm_differences[index][bits] = (float)difference;
		}
	}
}

static float adpcm_expand(int nibble, float *predictor, int *index)
{
	float difference = adpcm_differences[*index][nibble & 7];
	float value = *predictor + (nibble & 8 ? -difference : difference);

	if (value > 32767.0f)
		value = 32767.0f;
	else if (value < -32768.0f)
		value = -32768.0f;
	*predictor = value;
	*index += ima_index_table[nibble];
	if (*index < 0)
		*index = 0;
	else if (*index > IMA_STEP_COUNT - 1)
		*index = IMA_STEP_COUNT - 1;
	return value * (1.0f / 32768.0f);
}

/* the frames that size bytes of the voice's format decode to */
static unsigned long decoded_frames(const struct mixer_voice *voice, unsigned long size)
{
	if (voice->adpcm)
		return size / (XBOX_ADPCM_BLOCK_BYTES * voice->channels) * XBOX_ADPCM_BLOCK_SAMPLES;
	return size / (2 * voice->channels);
}

/* Xbox ADPCM, as dsound_sdl.c reads it: per block a 4-byte header per channel (little-endian predictor, step
index), then 4-byte groups of eight nibbles, low nibble first, alternating between channels; 64 samples */
static void decode_adpcm(const unsigned char *source, unsigned long size, unsigned long channels, float *samples)
{
	unsigned long block_bytes = XBOX_ADPCM_BLOCK_BYTES * channels;
	unsigned long blocks = size / block_bytes;
	unsigned long block, channel;

	for (block = 0; block < blocks; block++)
	{
		const unsigned char *data = source + block * block_bytes;
		float *output = samples + block * XBOX_ADPCM_BLOCK_SAMPLES * channels;

		for (channel = 0; channel < channels; channel++)
		{
			const unsigned char *header = data + channel * 4;
			float predictor = (float)(short)(header[0] | header[1] << 8);
			int index = header[2] > IMA_STEP_COUNT - 1 ? IMA_STEP_COUNT - 1 : header[2];
			unsigned long group, byte;

			for (group = 0; group < 8; group++)
			{
				const unsigned char *nibbles = data + 4 * channels + (group * channels + channel) * 4;

				for (byte = 0; byte < 4; byte++)
				{
					float *frame = output + (group * 8 + byte * 2) * channels + channel;

					frame[0] = adpcm_expand(nibbles[byte] & 0xf, &predictor, &index);
					frame[channels] = adpcm_expand(nibbles[byte] >> 4, &predictor, &index);
				}
			}
		}
	}
}

/* 16-bit PCM keeps the Xbox's little-endian order in memory */
static void decode_pcm(const unsigned char *source, unsigned long size, unsigned long channels, float *samples)
{
	unsigned long frames = size / (2 * channels);
	unsigned long sample;

	for (sample = 0; sample < frames * channels; sample++)
		samples[sample] = (float)(short)(source[sample * 2] | source[sample * 2 + 1] << 8) * (1.0f / 32768.0f);
}

/* decoded_frames(voice, size) frames into samples */
static void decode_into(const struct mixer_voice *voice, const void *source, unsigned long size, float *samples)
{
	if (voice->adpcm)
		decode_adpcm(source, size, voice->channels, samples);
	else
		decode_pcm(source, size, voice->channels, samples);
}

/* a buffer's data, decoded at once (SetBufferData) */
static float *decode(const struct mixer_voice *voice, const void *source, unsigned long size,
	unsigned long *frame_count)
{
	unsigned long frames = source ? decoded_frames(voice, size) : 0;
	float *samples = frames ? malloc(frames * voice->channels * sizeof(float)) : NULL;

	*frame_count = samples ? frames : 0;
	if (samples)
		decode_into(voice, source, size, samples);
	return samples;
}

/* ---------- 3D */

static float dot3(const float *a, const float *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void spatialize(const struct mixer_voice *voice, float *left, float *right)
{
	float offset[3], right_axis[3], distance, attenuation, side, ahead;
	int axis;

	if (voice->mode == DS3DMODE_HEADRELATIVE)
	{
		for (axis = 0; axis < 3; axis++)
			offset[axis] = voice->position[axis];
		side = offset[0];
		ahead = offset[2];
	}
	else
	{
		for (axis = 0; axis < 3; axis++)
			offset[axis] = voice->position[axis] - listener.position[axis];
		/* left-handed: right = top x front */
		right_axis[0] = listener.top[1] * listener.front[2] - listener.top[2] * listener.front[1];
		right_axis[1] = listener.top[2] * listener.front[0] - listener.top[0] * listener.front[2];
		right_axis[2] = listener.top[0] * listener.front[1] - listener.top[1] * listener.front[0];
		side = dot3(offset, right_axis);
		ahead = dot3(offset, listener.front);
	}
	distance = sqrtf(dot3(offset, offset)) * listener.distance_factor;

	/* DirectSound's inverse distance law, held beyond the maximum distance */
	attenuation = 1.0f;
	if (distance > voice->minimum_distance && voice->minimum_distance > 0.0f)
	{
		float clamped = distance < voice->maximum_distance ? distance : voice->maximum_distance;

		attenuation = voice->minimum_distance /
			(voice->minimum_distance + listener.rolloff_factor * (clamped - voice->minimum_distance));
	}

	/* equal power pan; close sources and sources straight ahead or behind stay centred, no ear below a quarter */
	{
		float horizontal = sqrtf(side * side + ahead * ahead);
		float pan = horizontal > 1.0e-4f ? side / horizontal : 0.0f;
		float angle;

		if (distance < voice->minimum_distance && voice->minimum_distance > 0.0f)
			pan *= distance / voice->minimum_distance;
		pan *= 0.75f;
		angle = (pan + 1.0f) * 0.25f * 3.14159265f;
		*left = cosf(angle);
		*right = sinf(angle);
	}
	*left *= attenuation * voice->i3dl2_gain;
	*right *= attenuation * voice->i3dl2_gain;
}

static void voice_gains(const struct mixer_voice *voice, float *left, float *right)
{
	if (voice->has_3d && voice->mode != DS3DMODE_DISABLE)
	{
		spatialize(voice, left, right);
	}
	else
	{
		*left = voice->mix_left;
		*right = voice->mix_right;
	}
	*left *= voice->volume * master_volume;
	*right *= voice->volume * master_volume;
}

/* ---------- mixing (the mixer thread: the streams without the lock, the buffers with it) */

static void mix_begin(struct mixer_voice *voice, unsigned long count, struct mix_state *state)
{
	double step = (double)(voice->frequency ? voice->frequency : voice->sample_rate) / OUTPUT_RATE;
	float target_left, target_right;

	state->step_whole = (unsigned long)step;
	state->step_fraction = (float)(step - (double)state->step_whole);
	voice_gains(voice, &target_left, &target_right);
	if (!voice->gains_valid)
	{
		voice->current_left = target_left;
		voice->current_right = target_right;
		voice->gains_valid = TRUE;
	}
	state->left = voice->current_left;
	state->right = voice->current_right;
	state->ramp_left = (target_left - state->left) / (float)count;
	state->ramp_right = (target_right - state->right) / (float)count;
	voice->current_left = target_left;
	voice->current_right = target_right;
}

/* mixes from the voice's position until count output frames are done or the position passes the last of frames
(whose final frame is held, not interpolated into what follows); returns the output frames done */
static unsigned long mix_run(struct mixer_voice *voice, const float *samples, unsigned long frames,
	struct mix_state *state, float *output, unsigned long count)
{
	unsigned long index = voice->index, done = 0;
	float fraction = voice->fraction, left = state->left, right = state->right;

	if (voice->channels == 1)
	{
		while (done < count && index < frames)
		{
			float a = samples[index];
			float b = index + 1 < frames ? samples[index + 1] : a;
			float sample = a + (b - a) * fraction;

			output[0] += sample * left;
			output[1] += sample * right;
			output += OUTPUT_CHANNELS;
			left += state->ramp_left;
			right += state->ramp_right;
			done++;
			index += state->step_whole;
			fraction += state->step_fraction;
			if (fraction >= 1.0f)
			{
				fraction -= 1.0f;
				index++;
			}
		}
	}
	else
	{
		while (done < count && index < frames)
		{
			const float *a = samples + index * 2;
			const float *b = index + 1 < frames ? a + 2 : a;

			output[0] += (a[0] + (b[0] - a[0]) * fraction) * left;
			output[1] += (a[1] + (b[1] - a[1]) * fraction) * right;
			output += OUTPUT_CHANNELS;
			left += state->ramp_left;
			right += state->ramp_right;
			done++;
			index += state->step_whole;
			fraction += state->step_fraction;
			if (fraction >= 1.0f)
			{
				fraction -= 1.0f;
				index++;
			}
		}
	}
	voice->index = index;
	voice->fraction = fraction;
	state->left = left;
	state->right = right;
	return done;
}

/* (with the lock) a pass takes the stream's unfinished packets; 0 when it has none */
static unsigned long stream_take_packets(struct ps3_stream *stream)
{
	unsigned long position;

	if (stream->restart)
	{
		stream->voice.index = 0;
		stream->voice.fraction = 0.0f;
		stream->voice.gains_valid = FALSE;
		stream->restart = FALSE;
	}
	stream->mix_count = 0;
	stream->mix_generation = stream->generation;
	if (!stream->voice.sample_rate)
		return 0;
	for (position = 0; position < stream->packet_count && stream->mix_count < MIX_PACKETS; position++)
	{
		struct voice_packet *slot = &stream->packets[(stream->packet_head + position) % MAXIMUM_STREAM_PACKETS];
		struct mix_packet *entry;

		if (slot->finished)
			continue;
		entry = &stream->mix_packets[stream->mix_count++];
		entry->slot = slot;
		entry->source = slot->source;
		entry->size = slot->size;
		entry->samples = slot->samples;
		entry->frames = slot->frames;
		entry->decoded = slot->decoded;
		entry->finished = FALSE;
	}
	return stream->mix_count;
}

/* (with the lock) gives the pass's decodes and finished packets back, unless a flush voided them */
static unsigned long stream_give_packets(struct ps3_stream *stream)
{
	unsigned long index, finished = 0;

	if (stream->mix_generation != stream->generation)
		return 0;
	for (index = 0; index < stream->mix_count; index++)
	{
		struct mix_packet *entry = &stream->mix_packets[index];

		entry->slot->decoded = entry->decoded;
		if (entry->finished)
		{
			entry->slot->finished = TRUE;
			finished++;
		}
	}
	return finished;
}

/* (without the lock) the packet's buffer stays until the pass ends (samples_release) */
static void packet_decode(const struct mixer_voice *voice, struct mix_packet *entry)
{
	uint64_t start = ps3_host_microseconds();

	if (entry->samples)
		decode_into(voice, entry->source, entry->size, entry->samples);
	entry->decoded = TRUE;
	packets_decoded++;
	mixer_decode_microseconds += (unsigned long)(ps3_host_microseconds() - start);
}

/* (without the lock) the packets the pass took, with its step and gains */
static void mix_stream(struct ps3_stream *stream, float *output, unsigned long count)
{
	struct mixer_voice *voice = &stream->voice;
	struct mix_state state = stream->mix_state;
	unsigned long packet = 0, done = 0;

	while (packet < stream->mix_count && done < count)
	{
		struct mix_packet *entry = &stream->mix_packets[packet];

		if (!entry->decoded)
			packet_decode(voice, entry);
		done += mix_run(voice, entry->samples, entry->frames, &state, output + done * OUTPUT_CHANNELS, count - done);
		if (voice->index >= entry->frames)
		{
			voice->index -= entry->frames;
			entry->finished = TRUE;
			packet++;
		}
	}
}

static void mix_buffer(struct ps3_buffer *buffer, float *output, unsigned long count)
{
	struct mixer_voice *voice = &buffer->voice;
	struct mix_state state;
	unsigned long done = 0;

	if (!buffer->playing || !buffer->frames || !voice->sample_rate)
		return;
	mix_begin(voice, count, &state);
	while (buffer->playing && done < count)
	{
		unsigned long end = buffer->looping ? buffer->loop_end : buffer->frames;

		done += mix_run(voice, buffer->samples, end, &state, output + done * OUTPUT_CHANNELS, count - done);
		if (voice->index < end)
			continue;
		if (buffer->looping && buffer->loop_end > buffer->loop_start)
		{
			voice->index = buffer->loop_start + (voice->index - end) % (buffer->loop_end - buffer->loop_start);
		}
		else
		{
			buffer->playing = FALSE;
			voice->index = 0;
			voice->fraction = 0.0f;
		}
	}
}

/* a stream's pass as an SPU voice; FALSE when an address does not fit the SPU's 32 bits */
static BOOL spu_voice_fill(spu_mixer_voice *record, const struct ps3_stream *stream)
{
	const struct mix_state *state = &stream->mix_state;
	unsigned long index;

	record->channels = stream->voice.channels;
	record->adpcm = stream->voice.adpcm ? 1 : 0;
	record->packet_count = stream->mix_count;
	record->index = stream->voice.index;
	record->fraction = stream->voice.fraction;
	record->step_whole = state->step_whole;
	record->step_fraction = state->step_fraction;
	record->left = state->left;
	record->right = state->right;
	record->ramp_left = state->ramp_left;
	record->ramp_right = state->ramp_right;
	for (index = 0; index < stream->mix_count; index++)
	{
		const struct mix_packet *entry = &stream->mix_packets[index];
		spu_mixer_packet *packet = &record->packets[index];

		if ((__uint64)(uintptr_t)entry->source > 0xffffffffu || (__uint64)(uintptr_t)entry->samples > 0xffffffffu)
			return FALSE;
		packet->source = (uint32_t)(uintptr_t)entry->source;
		packet->size = entry->size;
		packet->samples = (uint32_t)(uintptr_t)entry->samples;
		packet->frames = entry->frames;
		packet->flags = entry->decoded ? SPU_MIXER_DECODED : 0;
	}
	return TRUE;
}

/* the SPU's position, decodes and ends to the stream, as mix_stream leaves them */
static void spu_voice_apply(const spu_mixer_voice *record, struct ps3_stream *stream)
{
	unsigned long index;

	stream->voice.index = record->index;
	stream->voice.fraction = record->fraction;
	for (index = 0; index < stream->mix_count; index++)
	{
		stream->mix_packets[index].decoded = (record->packets[index].flags & SPU_MIXER_DECODED) != 0;
		stream->mix_packets[index].finished = (record->packets[index].flags & SPU_MIXER_FINISHED) != 0;
	}
	packets_decoded += record->decoded;
}

/* (without the lock) the taken streams as one SPU job into the empty output; returns the first stream it did not mix,
all of them when the SPU is off */
static struct ps3_stream *spu_mix(struct ps3_stream *taken, float *output, unsigned long count)
{
	spu_mixer_job *job = ps3_spu_mixer_job();
	struct ps3_stream *stream;
	uint32_t voices;

	if (!job)
		return taken;
	for (stream = taken, voices = 0; stream && voices < SPU_MIXER_VOICES; stream = stream->mix_next, voices++)
	{
		if (!spu_voice_fill(&job->voices[voices], stream))
			return taken;
	}
	if (!ps3_spu_mixer_run(voices, count))
		return taken;
	memcpy(output, job->output, count * OUTPUT_CHANNELS * sizeof(float));
	for (stream = taken, voices = 0; stream && voices < SPU_MIXER_VOICES; stream = stream->mix_next, voices++)
		spu_voice_apply(&job->voices[voices], stream);
	return stream;
}

/* a pass: the packets taken with the lock, decoded and mixed without it, their results given back with it */
static void mix(float *output, unsigned long count)
{
	struct ps3_stream *stream, *taken = NULL, **tail = &taken;
	struct ps3_buffer *buffer;
	unsigned long sample, finished = 0;

	memset(output, 0, count * OUTPUT_CHANNELS * sizeof(float));
	mixer_hold_begin();
	mixer_pass++;
	for (stream = streams; stream; stream = stream->next)
	{
		if (stream_take_packets(stream))
		{
			*tail = stream;
			tail = &stream->mix_next;
		}
	}
	*tail = NULL;
	mixer_hold_end();
	/* the steps and gains here; the decode and the mix on the SPU, or here when it is off */
	for (stream = taken; stream; stream = stream->mix_next)
		mix_begin(&stream->voice, count, &stream->mix_state);
	for (stream = taken ? spu_mix(taken, output, count) : NULL; stream; stream = stream->mix_next)
		mix_stream(stream, output, count);
	mixer_hold_begin();
	for (stream = taken; stream; stream = stream->mix_next)
		finished += stream_give_packets(stream);
	packets_finished += finished;
	for (buffer = buffers; buffer; buffer = buffer->next)
		mix_buffer(buffer, output, count);
	mixer_pass++;
	mixer_hold_end();
	/* soft limit rather than hard clip when many voices pile up */
	for (sample = 0; sample < count * OUTPUT_CHANNELS; sample++)
	{
		float value = output[sample];

		if (value > 0.8f || value < -0.8f)
		{
			float sign = value < 0.0f ? -1.0f : 1.0f;
			float excess = fabsf(value) - 0.8f;

			output[sample] = sign * (0.8f + 0.2f * tanhf(excess / 0.2f));
		}
	}
}

static void mix_timed(float *output, unsigned long count)
{
	uint64_t start = ps3_host_microseconds();
	unsigned long elapsed;

	mix(output, count);
	elapsed = (unsigned long)(ps3_host_microseconds() - start);
	mixer_chunks++;
	mixer_microseconds += elapsed;
	if (elapsed > mixer_maximum_microseconds)
		mixer_maximum_microseconds = elapsed;
}

/* ---------- output */

/* above the game's threads, as the Xbox's audio processor runs beside them; it sleeps between passes */
static void mixer_thread(void *argument)
{
	static float chunk[MIX_CHUNK_FRAMES * OUTPUT_CHANNELS];
	uint64_t next = ps3_host_microseconds();

	(void)argument;
	for (;;)
	{
		if (audio_port)
		{
			while (ps3_audio_queued_frames() + MIX_CHUNK_FRAMES <= QUEUE_TARGET_FRAMES)
			{
				mix_timed(chunk, MIX_CHUNK_FRAMES);
				/* short only after ps3_audio_shutdown: the process exits */
				if (ps3_audio_write(chunk, MIX_CHUNK_FRAMES) < MIX_CHUNK_FRAMES)
					return;
			}
			ps3_host_sleep_microseconds(MIX_SLEEP_MICROSECONDS);
		}
		else
		{
			uint64_t now;

			/* no audio port: mix into nothing in real time, so streams still drain */
			mix_timed(chunk, MIX_CHUNK_FRAMES);
			next += MIX_CHUNK_MICROSECONDS;
			now = ps3_host_microseconds();
			if (next > now)
				ps3_host_sleep_microseconds(next - now);
			else
				next = now;
		}
	}
}

static void audio_start(void)
{
	if (audio_started)
		return;
	audio_started = TRUE;
	if (!mixer_lock)
		mixer_lock = ps3_host_lock_create(FALSE);
	adpcm_tables_initialize();
	master_volume = (float)config_real("audio.volume");
	if (config_boolean("audio.enabled"))
		audio_port = ps3_audio_init();
	platform_log("DirectSound: the streams decode and mix on %s", ps3_spu_mixer_start() ? "an SPU" : "the PPE");
	if (ps3_host_thread_create(mixer_thread, NULL, MIXER_STACK_SIZE, PS3_HOST_PRIORITY_NORMAL - 1, "dsound") != 0)
		platform_log("DirectSound: cannot start the mixer thread");
	else if (audio_port)
		platform_log("DirectSound: the PS3 audio port, %d Hz stereo, mixed %d ms ahead", OUTPUT_RATE,
			(QUEUE_TARGET_FRAMES * 1000 + OUTPUT_RATE / 2) / OUTPUT_RATE);
	else
		platform_log("DirectSound: silent (audio.enabled is false, or the audio port did not open)");
}

/* every 10 s on the game's thread: underruns show as silent or partial port blocks while voices play */
static void statistics_log(void)
{
	static uint64_t next;
	static unsigned last_silent, last_partial;
	uint64_t now = ps3_host_microseconds();
	unsigned long chunks, microseconds, maximum, decode, held, held_maximum, decoded;
	unsigned long in_use, peak, blocks, block_bytes;
	unsigned long playing_streams = 0, playing_buffers = 0;
	unsigned silent = 0, partial = 0;
	struct ps3_spu_mixer_statistics spu;
	char spu_text[256];
	struct ps3_stream *stream;
	struct ps3_buffer *buffer;

	if (now < next)
		return;
	next = now + STATISTICS_MICROSECONDS;
	mixer_enter();
	for (stream = streams; stream; stream = stream->next)
		playing_streams += stream->packet_count != 0;
	for (buffer = buffers; buffer; buffer = buffer->next)
		playing_buffers += buffer->playing && buffer->voice.current_left + buffer->voice.current_right > 0.0f;
	chunks = mixer_chunks;
	microseconds = mixer_microseconds;
	maximum = mixer_maximum_microseconds;
	decode = mixer_decode_microseconds;
	held = mixer_hold_microseconds;
	held_maximum = mixer_hold_maximum_microseconds;
	decoded = packets_decoded;
	in_use = samples_store.in_use;
	peak = samples_store.peak;
	blocks = samples_store.blocks;
	block_bytes = samples_store.block_bytes;
	mixer_chunks = 0;
	mixer_microseconds = 0;
	mixer_maximum_microseconds = 0;
	mixer_decode_microseconds = 0;
	mixer_hold_microseconds = 0;
	mixer_hold_maximum_microseconds = 0;
	packets_decoded = 0;
	mixer_leave();
	if (audio_port)
		ps3_audio_stats(&silent, &partial);
	/* the SPU's time a job (a chunk), the mixer thread's waits for it, and how they ended */
	ps3_spu_mixer_statistics(&spu);
	if (spu.jobs)
		snprintf(spu_text, sizeof(spu_text), "SPU %lu us a chunk, decoding %lu, the mixer's wait %lu; waits ended by "
			"the event %lu, the 1 ms timeout %lu, errors %lu; events refused %lu", (unsigned long)(spu.busy_microseconds /
			spu.jobs), (unsigned long)(spu.decode_microseconds / spu.jobs), (unsigned long)(spu.wait_microseconds /
			spu.jobs), (unsigned long)spu.events, (unsigned long)spu.timeouts, (unsigned long)spu.receive_errors,
			(unsigned long)spu.event_errors);
	else
		snprintf(spu_text, sizeof(spu_text), "SPU %s", spu.up ? "idle" : "off");
	/* the mixer's lock: held twice a chunk (a pass); the mixer's time includes its wait for the SPU */
	platform_log("DirectSound: %lu streams and %lu buffers playing, %lu packets decoded; mixer %lu us a chunk "
		"(max %lu) of %d, decoding %lu, the lock held %lu (max %lu); port blocks %u silent, %u partial; %s; packet "
		"buffers %lu KB (peak %lu) of %lu KB in %lu blocks", playing_streams, playing_buffers, decoded,
		chunks ? microseconds / chunks : 0, maximum, MIX_CHUNK_MICROSECONDS, chunks ? decode / chunks : 0,
		chunks ? held / chunks : 0, held_maximum, silent - last_silent, partial - last_partial, spu_text, in_use / 1024,
		peak / 1024, block_bytes / 1024, blocks);
	last_silent = silent;
	last_partial = partial;
}

/* ---------- completion */

/* gives back the head packet; with the lock held, which the game's callback runs without */
static void stream_complete_head(struct ps3_stream *stream, DWORD status, DWORD completed_size)
{
	struct voice_packet *entry = &stream->packets[stream->packet_head];
	XMEDIAPACKET packet = entry->packet;
	float *samples = entry->samples;
	unsigned long bytes = entry->bytes;
	BOOL finished = entry->finished;

	entry->samples = NULL;
	entry->decoded = FALSE;
	entry->finished = FALSE;
	stream->packet_head = (stream->packet_head + 1) % MAXIMUM_STREAM_PACKETS;
	stream->packet_count--;
	samples_release(samples, bytes, finished);
	if (packet.pdwCompletedSize)
		*packet.pdwCompletedSize = completed_size;
	if (packet.pdwStatus)
		*packet.pdwStatus = status;
	if (stream->callback)
	{
		mixer_leave();
		stream->callback(stream->context, packet.pContext, status);
		mixer_enter();
	}
	else if (packet.hCompletionEvent)
	{
		SetEvent(packet.hCompletionEvent);
	}
}

static void streams_complete_finished(void)
{
	struct ps3_stream *stream;

	/* nothing finished since the last time: no lock */
	if (packets_finished == packets_completed)
		return;
	mixer_enter();
	packets_completed = packets_finished;
	for (stream = streams; stream; stream = stream->next)
	{
		while (stream->packet_count && stream->packets[stream->packet_head].finished)
			stream_complete_head(stream, XMEDIAPACKET_STATUS_SUCCESS, stream->packets[stream->packet_head].packet.dwMaxSize);
	}
	mixer_leave();
}

/* ---------- the stream interface (the game's thread; the count and the packet ring's ends are its own) */

static ULONG STDMETHODCALLTYPE stream_add_reference(IDirectSoundStream *object)
{
	struct ps3_stream *stream = (struct ps3_stream *)object;

	return ++stream->reference_count;
}

static HRESULT STDMETHODCALLTYPE stream_flush(IDirectSoundStream *object)
{
	struct ps3_stream *stream = (struct ps3_stream *)object;

	mixer_enter();
	while (stream->packet_count)
	{
		struct voice_packet *head = &stream->packets[stream->packet_head];

		/* (each time: a pass can start while a callback runs without the lock) */
		stream->generation++;
		stream_complete_head(stream, head->finished ? XMEDIAPACKET_STATUS_SUCCESS : XMEDIAPACKET_STATUS_FLUSHED,
			head->finished ? head->packet.dwMaxSize : 0);
	}
	stream->restart = TRUE;
	mixer_leave();
	return S_OK;
}

static ULONG STDMETHODCALLTYPE stream_release(IDirectSoundStream *object)
{
	struct ps3_stream *stream = (struct ps3_stream *)object;
	struct ps3_stream **link;
	unsigned long pass;

	if (--stream->reference_count)
		return stream->reference_count;
	stream_flush(object);
	mixer_enter();
	for (link = &streams; *link; link = &(*link)->next)
	{
		if (*link == stream)
		{
			*link = stream->next;
			break;
		}
	}
	pass = mixer_pass;
	mixer_leave();
	/* a pass that took it reads it until its end */
	if (pass & 1)
		mixer_wait_pass(pass);
	free(stream);
	return 0;
}

static HRESULT STDMETHODCALLTYPE stream_get_info(IDirectSoundStream *object, LPXMEDIAINFO information)
{
	struct ps3_stream *stream = (struct ps3_stream *)object;

	memset(information, 0, sizeof(*information));
	information->dwFlags = XMO_STREAMF_FIXED_SAMPLE_SIZE | XMO_STREAMF_INPUT_ASYNC;
	information->dwInputSize = stream->voice.adpcm ? XBOX_ADPCM_BLOCK_BYTES * stream->voice.channels :
		2 * stream->voice.channels;
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_get_status(IDirectSoundStream *object, LPDWORD status)
{
	struct ps3_stream *stream = (struct ps3_stream *)object;

	*status = stream->packet_count < MAXIMUM_STREAM_PACKETS ? XMO_STATUSF_ACCEPT_INPUT_DATA : 0;
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_process(IDirectSoundStream *object, LPCXMEDIAPACKET input, LPCXMEDIAPACKET output)
{
	struct ps3_stream *stream = (struct ps3_stream *)object;
	struct voice_packet *entry;
	unsigned long frames, bytes = 0;
	float *samples = NULL;

	(void)output;
	if (!input)
		return E_INVALIDARG;
	if (stream->packet_count == MAXIMUM_STREAM_PACKETS)
		return E_OUTOFMEMORY;
	/* only the buffer here, which the SPU's DMA can write: the mixer decodes it (packet_decode) */
	frames = input->pvBuffer ? decoded_frames(&stream->voice, input->dwMaxSize) : 0;
	mixer_enter();
	if (frames)
		samples = samples_take((frames * stream->voice.channels * sizeof(float) + 127) & ~127ul, &bytes);
	entry = &stream->packets[(stream->packet_head + stream->packet_count) % MAXIMUM_STREAM_PACKETS];
	entry->packet = *input;
	entry->source = input->pvBuffer;
	entry->size = input->dwMaxSize;
	entry->samples = samples;
	entry->frames = samples ? frames : 0;
	entry->bytes = bytes;
	entry->decoded = FALSE;
	entry->finished = FALSE;
	if (input->pdwStatus)
		*input->pdwStatus = XMEDIAPACKET_STATUS_PENDING;
	if (input->pdwCompletedSize)
		*input->pdwCompletedSize = 0;
	/* a stream that ran dry starts over */
	if (!stream->packet_count)
		stream->restart = TRUE;
	stream->packet_count++;
	mixer_leave();
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_discontinuity(IDirectSoundStream *object)
{
	(void)object;
	return S_OK;
}

static IDirectSoundStreamVtbl stream_vtable =
{
	stream_add_reference,
	stream_release,
	stream_get_info,
	stream_get_status,
	stream_process,
	stream_discontinuity,
	stream_flush,
};

/* (sound_dsound_xbox.c) a stereo stream placed in the world: not yet, so it plays as it is (dsound_sdl.c pans it) */
void dsound_sdl_stream_set_stereo_position(IDirectSoundStream *object, BOOL positioned, float pan,
	float distance, float minimum_distance, float distance_fade)
{
	(void)object;
	(void)positioned;
	(void)pan;
	(void)distance;
	(void)minimum_distance;
	(void)distance_fade;
}

/* ---------- the DirectSound object */

static ULONG direct_sound_references;

HRESULT WINAPI DirectSoundCreate(LPGUID device_id, LPDIRECTSOUND *result, LPUNKNOWN outer)
{
	(void)device_id;
	(void)outer;
	audio_start();
	mixer_enter();
	direct_sound_references++;
	mixer_leave();
	*result = (LPDIRECTSOUND)&direct_sound_references;
	return DS_OK;
}

ULONG WINAPI IDirectSound_Release(LPDIRECTSOUND sound)
{
	ULONG count;

	(void)sound;
	mixer_enter();
	count = direct_sound_references ? --direct_sound_references : 0;
	mixer_leave();
	return count;
}

VOID WINAPI DirectSoundDoWork(void)
{
	static unsigned long volume_read_at = (unsigned long)-1;

	/* the config's file reads stay outside the mixer's lock */
	if (volume_read_at != config_changes())
	{
		float volume;

		volume_read_at = config_changes();
		volume = (float)config_real("audio.volume");
		mixer_enter();
		master_volume = volume;
		mixer_leave();
	}
	streams_complete_finished();
	if (retired_count)
	{
		mixer_enter();
		retired_free();
		mixer_leave();
	}
	statistics_log();
}

VOID WINAPI DirectSoundUseFullHRTF(void)
{
}

HRESULT WINAPI IDirectSound_GetCaps(LPDIRECTSOUND sound, LPDSCAPS caps)
{
	(void)sound;
	memset(caps, 0, sizeof(*caps));
	caps->dwFree2DBuffers = 64;
	caps->dwFree3DBuffers = 64;
	caps->dwFreeBufferSGEs = 2047;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_GetSpeakerConfig(LPDIRECTSOUND sound, LPDWORD speaker_config)
{
	(void)sound;
	*speaker_config = DSSPEAKER_STEREO;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_DownloadEffectsImage(LPDIRECTSOUND sound, LPCVOID image, DWORD image_size,
	LPCDSEFFECTIMAGELOC image_location, LPDSEFFECTIMAGEDESC *image_description)
{
	(void)sound;
	(void)image;
	(void)image_size;
	(void)image_location;
	if (image_description)
		*image_description = NULL;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_CommitDeferredSettings(LPDIRECTSOUND sound) { (void)sound; return DS_OK; }
HRESULT WINAPI IDirectSound_SetMixBinHeadroom(LPDIRECTSOUND sound, DWORD mix_bin_mask, DWORD headroom) { (void)sound; (void)mix_bin_mask; (void)headroom; return DS_OK; }
HRESULT WINAPI IDirectSound_SetI3DL2Listener(LPDIRECTSOUND sound, LPCDSI3DL2LISTENER listener_properties, DWORD apply) { (void)sound; (void)listener_properties; (void)apply; return DS_OK; }
HRESULT WINAPI IDirectSound_SetVelocity(LPDIRECTSOUND sound, FLOAT x, FLOAT y, FLOAT z, DWORD apply) { (void)sound; (void)x; (void)y; (void)z; (void)apply; return DS_OK; }

/* the listener's settings, as a voice's (STREAM_SETTER) */
HRESULT WINAPI IDirectSound_SetDistanceFactor(LPDIRECTSOUND sound, FLOAT factor, DWORD apply)
{
	(void)sound;
	(void)apply;
	listener.distance_factor = factor > 0.0f ? factor : 1.0f;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetRolloffFactor(LPDIRECTSOUND sound, FLOAT factor, DWORD apply)
{
	(void)sound;
	(void)apply;
	listener.rolloff_factor = factor >= 0.0f ? factor : 1.0f;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetPosition(LPDIRECTSOUND sound, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)sound;
	(void)apply;
	listener.position[0] = x;
	listener.position[1] = y;
	listener.position[2] = z;
	return DS_OK;
}

static void normalize3(float *vector)
{
	float length = sqrtf(dot3(vector, vector));

	if (length > 1.0e-6f)
	{
		vector[0] /= length;
		vector[1] /= length;
		vector[2] /= length;
	}
}

HRESULT WINAPI IDirectSound_SetOrientation(LPDIRECTSOUND sound, FLOAT x_front, FLOAT y_front, FLOAT z_front,
	FLOAT x_top, FLOAT y_top, FLOAT z_top, DWORD apply)
{
	float front[3], top[3];

	(void)sound;
	(void)apply;
	/* normalized before the mixer can read them */
	front[0] = x_front;
	front[1] = y_front;
	front[2] = z_front;
	top[0] = x_top;
	top[1] = y_top;
	top[2] = z_top;
	normalize3(front);
	normalize3(top);
	memcpy(listener.front, front, sizeof(front));
	memcpy(listener.top, top, sizeof(top));
	return DS_OK;
}

HRESULT WINAPI IDirectSound_CreateSoundStream(LPDIRECTSOUND sound, LPCDSSTREAMDESC description,
	LPDIRECTSOUNDSTREAM *result, LPUNKNOWN outer)
{
	struct ps3_stream *stream = calloc(1, sizeof(*stream));

	(void)sound;
	(void)outer;
	if (!stream)
		return E_OUTOFMEMORY;
	stream->object.lpVtbl = &stream_vtable;
	stream->reference_count = 1;
	stream->callback = description->lpfnCallback;
	stream->context = description->lpvContext;
	voice_initialize(&stream->voice, description->lpwfxFormat, (description->dwFlags & DSSTREAMCAPS_CTRL3D) != 0);
	mixer_enter();
	stream->next = streams;
	streams = stream;
	mixer_leave();
	*result = &stream->object;
	return DS_OK;
}

/* January-era exports the game declares itself (sound_dsound_xbox.c) */

void __stdcall DirectSoundStopStream(LPDIRECTSOUNDSTREAM stream)
{
	stream_flush(stream);
}

unsigned long __stdcall DirectSoundGetStreamVoiceStatus(LPDIRECTSOUNDSTREAM stream)
{
	struct ps3_stream *record = (struct ps3_stream *)stream;

	return record->packet_count != 0;
}

/* a voice's setting without the mixer's lock: words the mixer reads once a chunk (one chunk can take a part of a
change), which the game sets hundreds of times a frame */
#define STREAM_SETTER(body) \
	struct ps3_stream *record = (struct ps3_stream *)stream; \
	body; \
	return DS_OK;

HRESULT WINAPI IDirectSoundStream_SetFrequency(LPDIRECTSOUNDSTREAM stream, DWORD frequency)
{
	STREAM_SETTER(record->voice.frequency = frequency ? frequency : record->voice.sample_rate)
}

HRESULT WINAPI IDirectSoundStream_SetVolume(LPDIRECTSOUNDSTREAM stream, LONG volume)
{
	float gain = gain_from_millibels(volume);

	STREAM_SETTER(record->voice.volume = gain)
}

HRESULT WINAPI IDirectSoundStream_SetMixBins(LPDIRECTSOUNDSTREAM stream, DWORD mix_bin_mask)
{
	STREAM_SETTER(
		record->voice.mix_left = (mix_bin_mask & DSMIXBIN_FRONT_LEFT) ? 1.0f : 0.0f;
		record->voice.mix_right = (mix_bin_mask & DSMIXBIN_FRONT_RIGHT) ? 1.0f : 0.0f)
}

/* the volumes come in the order of the mask's set bits */
HRESULT WINAPI IDirectSoundStream_SetMixBinVolumes(LPDIRECTSOUNDSTREAM stream, DWORD mix_bin_mask, const LONG *volumes)
{
	struct ps3_stream *record = (struct ps3_stream *)stream;
	unsigned long bit, index = 0;

	/* (settings: STREAM_SETTER's) */
	for (bit = 0; bit < 32; bit++)
	{
		if (!(mix_bin_mask & (1UL << bit)))
			continue;
		if ((1UL << bit) == DSMIXBIN_FRONT_LEFT)
			record->voice.mix_left = gain_from_millibels(volumes[index]);
		else if ((1UL << bit) == DSMIXBIN_FRONT_RIGHT)
			record->voice.mix_right = gain_from_millibels(volumes[index]);
		index++;
	}
	return DS_OK;
}

HRESULT WINAPI IDirectSoundStream_SetMode(LPDIRECTSOUNDSTREAM stream, DWORD mode, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->voice.mode = mode)
}

HRESULT WINAPI IDirectSoundStream_SetPosition(LPDIRECTSOUNDSTREAM stream, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->voice.position[0] = x; record->voice.position[1] = y; record->voice.position[2] = z)
}

HRESULT WINAPI IDirectSoundStream_SetMinDistance(LPDIRECTSOUNDSTREAM stream, FLOAT distance, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->voice.minimum_distance = distance)
}

HRESULT WINAPI IDirectSoundStream_SetMaxDistance(LPDIRECTSOUNDSTREAM stream, FLOAT distance, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->voice.maximum_distance = distance)
}

HRESULT WINAPI IDirectSoundStream_SetI3DL2Source(LPDIRECTSOUNDSTREAM stream, LPCDSI3DL2BUFFER source, DWORD apply)
{
	LONG direct;
	float gain;

	(void)apply;
	/* the low frequency part of the direct path */
	direct = source->lDirect +
		(LONG)(source->Obstruction.lHFLevel * source->Obstruction.flLFRatio) +
		(LONG)(source->Occlusion.lHFLevel * source->Occlusion.flLFRatio);
	if (direct > 0)
		direct = 0;
	gain = gain_from_millibels(direct);
	{
		STREAM_SETTER(record->voice.i3dl2_gain = gain)
	}
}

HRESULT WINAPI IDirectSoundStream_SetVelocity(LPDIRECTSOUNDSTREAM stream, FLOAT x, FLOAT y, FLOAT z, DWORD apply) { (void)stream; (void)x; (void)y; (void)z; (void)apply; return DS_OK; }
HRESULT WINAPI IDirectSoundStream_SetConeAngles(LPDIRECTSOUNDSTREAM stream, DWORD inside, DWORD outside, DWORD apply) { (void)stream; (void)inside; (void)outside; (void)apply; return DS_OK; }
HRESULT WINAPI IDirectSoundStream_SetConeOrientation(LPDIRECTSOUNDSTREAM stream, FLOAT x, FLOAT y, FLOAT z, DWORD apply) { (void)stream; (void)x; (void)y; (void)z; (void)apply; return DS_OK; }
HRESULT WINAPI IDirectSoundStream_SetConeOutsideVolume(LPDIRECTSOUNDSTREAM stream, LONG volume, DWORD apply) { (void)stream; (void)volume; (void)apply; return DS_OK; }

/* ---------- buffers */

HRESULT WINAPI DirectSoundCreateBuffer(LPCDSBUFFERDESC description, LPDIRECTSOUNDBUFFER *result)
{
	struct ps3_buffer *buffer = calloc(1, sizeof(*buffer));

	if (!buffer)
		return E_OUTOFMEMORY;
	audio_start();
	buffer->reference_count = 1;
	voice_initialize(&buffer->voice, description->lpwfxFormat, FALSE);
	/* 0 keeps the default bins; the inanity buffer's (crosstalk, reverb, sends) reach no front speaker */
	if (description->dwMixBinMask)
	{
		buffer->voice.mix_left = (description->dwMixBinMask & DSMIXBIN_FRONT_LEFT) ? 1.0f : 0.0f;
		buffer->voice.mix_right = (description->dwMixBinMask & DSMIXBIN_FRONT_RIGHT) ? 1.0f : 0.0f;
	}
	mixer_enter();
	buffer->next = buffers;
	buffers = buffer;
	mixer_leave();
	*result = (LPDIRECTSOUNDBUFFER)buffer;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_CreateSoundBuffer(LPDIRECTSOUND sound, LPCDSBUFFERDESC description,
	LPDIRECTSOUNDBUFFER *result, LPUNKNOWN outer)
{
	(void)sound;
	(void)outer;
	return DirectSoundCreateBuffer(description, result);
}

ULONG WINAPI IDirectSoundBuffer_Release(LPDIRECTSOUNDBUFFER buffer)
{
	struct ps3_buffer *record = (struct ps3_buffer *)buffer;
	struct ps3_buffer **link;
	ULONG count;

	mixer_enter();
	count = --record->reference_count;
	if (!count)
	{
		for (link = &buffers; *link; link = &(*link)->next)
		{
			if (*link == record)
			{
				*link = record->next;
				break;
			}
		}
	}
	mixer_leave();
	if (!count)
	{
		free(record->samples);
		free(record);
	}
	return count;
}

static unsigned long buffer_frames_from_bytes(const struct ps3_buffer *buffer, DWORD bytes)
{
	if (buffer->voice.adpcm)
		return bytes / (XBOX_ADPCM_BLOCK_BYTES * buffer->voice.channels) * XBOX_ADPCM_BLOCK_SAMPLES;
	return bytes / (2 * buffer->voice.channels);
}

/* the Xbox plays the caller's memory in place; it is decoded once, here */
HRESULT WINAPI IDirectSoundBuffer_SetBufferData(LPDIRECTSOUNDBUFFER buffer, LPVOID data, DWORD size)
{
	struct ps3_buffer *record = (struct ps3_buffer *)buffer;
	unsigned long frames = 0;
	float *samples = decode(&record->voice, data, size, &frames), *previous;

	mixer_enter();
	previous = record->samples;
	record->samples = samples;
	record->frames = samples ? frames : 0;
	record->loop_start = 0;
	record->loop_end = record->frames;
	record->voice.index = 0;
	record->voice.fraction = 0.0f;
	mixer_leave();
	free(previous);
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_Play(LPDIRECTSOUNDBUFFER buffer, DWORD reserved1, DWORD reserved2, DWORD flags)
{
	struct ps3_buffer *record = (struct ps3_buffer *)buffer;

	(void)reserved1;
	(void)reserved2;
	mixer_enter();
	if (!record->playing)
		record->voice.gains_valid = FALSE;
	record->playing = TRUE;
	record->looping = (flags & DSBPLAY_LOOPING) != 0;
	mixer_leave();
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_Stop(LPDIRECTSOUNDBUFFER buffer)
{
	struct ps3_buffer *record = (struct ps3_buffer *)buffer;

	mixer_enter();
	record->playing = FALSE;
	mixer_leave();
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_SetCurrentPosition(LPDIRECTSOUNDBUFFER buffer, DWORD play_cursor)
{
	struct ps3_buffer *record = (struct ps3_buffer *)buffer;

	mixer_enter();
	record->voice.index = buffer_frames_from_bytes(record, play_cursor);
	record->voice.fraction = 0.0f;
	mixer_leave();
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_SetLoopRegion(LPDIRECTSOUNDBUFFER buffer, DWORD loop_start, DWORD loop_length)
{
	struct ps3_buffer *record = (struct ps3_buffer *)buffer;

	mixer_enter();
	record->loop_start = buffer_frames_from_bytes(record, loop_start);
	record->loop_end = buffer_frames_from_bytes(record, loop_start + loop_length);
	if (record->loop_end > record->frames)
		record->loop_end = record->frames;
	if (record->loop_start >= record->loop_end)
	{
		record->loop_start = 0;
		record->loop_end = record->frames;
	}
	mixer_leave();
	return DS_OK;
}

/* in 1/4096 octave from 48 kHz: the loading screen's -4681 plays its 22 kHz tone near its own rate */
HRESULT WINAPI IDirectSoundBuffer_SetPitch(LPDIRECTSOUNDBUFFER buffer, LONG pitch)
{
	struct ps3_buffer *record = (struct ps3_buffer *)buffer;
	double frequency = OUTPUT_RATE * pow(2.0, pitch / 4096.0);

	if (frequency < DSBFREQUENCY_MIN)
		frequency = DSBFREQUENCY_MIN;
	else if (frequency > DSBFREQUENCY_MAX)
		frequency = DSBFREQUENCY_MAX;
	/* (settings: STREAM_SETTER's) */
	record->voice.frequency = (DWORD)frequency;
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_SetVolume(LPDIRECTSOUNDBUFFER buffer, LONG volume)
{
	struct ps3_buffer *record = (struct ps3_buffer *)buffer;

	record->voice.volume = gain_from_millibels(volume);
	return DS_OK;
}
