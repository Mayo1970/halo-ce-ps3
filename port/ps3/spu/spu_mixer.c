/* SPU_MIXER.C: an SPU thread that decodes and mixes the DirectSound streams of a chunk for dsound_ps3.c: the
operations of its mix_stream, in the same order. Between jobs it sleeps on a reservation of the bell's line. */

#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include <sys/spu_event.h>
#include <sys/spu_thread.h>

#include <stddef.h>
#include <string.h>

#include "spu_mixer.h"

#define TAG_JOB 1
#define TAG_INPUT 2
/* and TAG_OUTPUT + 1: the decode's two buffers */
#define TAG_OUTPUT 3
/* 1 ms of the timebase: a wake if a reservation event is lost */
#define DECREMENTER_TICKS 79800
/* 10 KB of voices: one DMA */
#define VOICE_BATCH 32
/* a decode batch's floats (16 KB, one DMA), and a mix window's */
#define DECODE_FLOATS 4096
#define WINDOW_FLOATS 2048
/* a fetch holds the lines around its bytes: up to 30 more */
#define FETCH_SLOP 32

#define ADPCM_BLOCK_BYTES 36
#define ADPCM_BLOCK_SAMPLES 64
#define IMA_STEP_COUNT 89

enum
{
	_stage_idle,
	_stage_header,
	_stage_voices,
	_stage_decode,
	_stage_mix,
	_stage_results,
	_stage_event,
};

/* the chunk's resampling step and gain ramp of one voice */
struct mix_state
{
	uint32_t step_whole;
	float step_fraction;
	float left, right;
	float ramp_left, ramp_right;
};

static spu_mixer_bell bell;
static spu_mixer_echo echo;
static spu_mixer_header header __attribute__((aligned(128)));
static spu_mixer_voice voices[VOICE_BATCH] __attribute__((aligned(128)));
static float output[SPU_MIXER_FRAMES * 2] __attribute__((aligned(128)));
static float decoded[2][DECODE_FLOATS] __attribute__((aligned(128)));
static float window[WINDOW_FLOATS + FETCH_SLOP / 4] __attribute__((aligned(128)));
static unsigned char input[8192 + FETCH_SLOP] __attribute__((aligned(128)));
static uint64_t bell_ea, echo_ea, job_ea;

/* ---------- DMA */

static void dma_wait(uint32_t tags)
{
	mfc_write_tag_mask(tags);
	mfc_read_tag_status_all();
}

static void put_echo(void)
{
	mfc_put(&echo, echo_ea, sizeof(echo), TAG_JOB, 0, 0);
	dma_wait(1 << TAG_JOB);
}

/* size bytes (1 to 8192) at ea into buffer, with the 16-byte lines that hold them; returns where ea's byte is */
static const void *fetch(void *buffer, uint32_t ea, uint32_t size)
{
	uint32_t first = ea & ~15u, end = (ea + size + 15) & ~15u;

	mfc_get(buffer, first, end - first, TAG_INPUT, 0, 0);
	dma_wait(1 << TAG_INPUT);
	return (const unsigned char *)buffer + (ea & 15);
}

/* ---------- decoding (dsound_ps3.c's) */

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

/* whole numbers in floats: the decode is exact in any rounding mode, so it matches the PPE's bit for bit */
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

/* inline and with selects, not branches: an SPU branch without a hint costs 18 cycles */
static inline __attribute__((always_inline)) float adpcm_expand(int nibble, float *predictor, int *index)
{
	float difference = adpcm_differences[*index][nibble & 7];
	float value = *predictor + (nibble & 8 ? -difference : difference);
	int next = *index + ima_index_table[nibble];

	value = value > 32767.0f ? 32767.0f : value;
	value = value < -32768.0f ? -32768.0f : value;
	*predictor = value;
	next = next < 0 ? 0 : next;
	*index = next > IMA_STEP_COUNT - 1 ? IMA_STEP_COUNT - 1 : next;
	return value * (1.0f / 32768.0f);
}

static void decode_adpcm(const unsigned char *source, uint32_t size, uint32_t channels, float *samples)
{
	uint32_t block_bytes = ADPCM_BLOCK_BYTES * channels;
	uint32_t blocks = size / block_bytes;
	uint32_t block, channel;

	for (block = 0; block < blocks; block++)
	{
		const unsigned char *data = source + block * block_bytes;
		float *frames = samples + block * ADPCM_BLOCK_SAMPLES * channels;

		for (channel = 0; channel < channels; channel++)
		{
			const unsigned char *block_header = data + channel * 4;
			float predictor = (float)(short)(block_header[0] | block_header[1] << 8);
			int index = block_header[2] > IMA_STEP_COUNT - 1 ? IMA_STEP_COUNT - 1 : block_header[2];
			uint32_t group, byte;

			for (group = 0; group < 8; group++)
			{
				const unsigned char *nibbles = data + 4 * channels + (group * channels + channel) * 4;

				for (byte = 0; byte < 4; byte++)
				{
					float *frame = frames + (group * 8 + byte * 2) * channels + channel;

					frame[0] = adpcm_expand(nibbles[byte] & 0xf, &predictor, &index);
					frame[channels] = adpcm_expand(nibbles[byte] >> 4, &predictor, &index);
				}
			}
		}
	}
}

static void decode_pcm(const unsigned char *source, uint32_t size, uint32_t channels, float *samples)
{
	uint32_t frames = size / (2 * channels);
	uint32_t sample;

	for (sample = 0; sample < frames * channels; sample++)
		samples[sample] = (float)(short)(source[sample * 2] | source[sample * 2 + 1] << 8) * (1.0f / 32768.0f);
}

/* the packet's floats into its buffer, a batch of 16 KB at a time; a batch's put runs while the next one decodes */
static void decode_packet(const spu_mixer_voice *voice, const spu_mixer_packet *packet)
{
	uint32_t channels = voice->channels, batch = 0;

	if (voice->adpcm)
	{
		uint32_t block_bytes = ADPCM_BLOCK_BYTES * channels;
		uint32_t blocks = packet->size / block_bytes, batch_blocks = DECODE_FLOATS / (ADPCM_BLOCK_SAMPLES * channels);
		uint32_t block;

		if (blocks > packet->frames / ADPCM_BLOCK_SAMPLES)
			blocks = packet->frames / ADPCM_BLOCK_SAMPLES;
		for (block = 0; block < blocks; block += batch_blocks, batch++)
		{
			uint32_t count = blocks - block < batch_blocks ? blocks - block : batch_blocks;
			float *floats = decoded[batch & 1];
			const unsigned char *source = fetch(input, packet->source + block * block_bytes, count * block_bytes);

			dma_wait(1 << (TAG_OUTPUT + (batch & 1)));
			decode_adpcm(source, count * block_bytes, channels, floats);
			mfc_put(floats, packet->samples + block * ADPCM_BLOCK_SAMPLES * channels * 4,
				count * ADPCM_BLOCK_SAMPLES * channels * 4, TAG_OUTPUT + (batch & 1), 0, 0);
		}
	}
	else
	{
		uint32_t frames = packet->size / (2 * channels), batch_frames = DECODE_FLOATS / channels;
		uint32_t frame;

		if (frames > packet->frames)
			frames = packet->frames;
		for (frame = 0; frame < frames; frame += batch_frames, batch++)
		{
			uint32_t count = frames - frame < batch_frames ? frames - frame : batch_frames;
			float *floats = decoded[batch & 1];
			const unsigned char *source = fetch(input, packet->source + frame * 2 * channels, count * 2 * channels);

			dma_wait(1 << (TAG_OUTPUT + (batch & 1)));
			decode_pcm(source, count * 2 * channels, channels, floats);
			/* (a last batch's whole lines: the buffer's size is rounded up for them) */
			mfc_put(floats, packet->samples + frame * channels * 4, (count * channels * 4 + 15) & ~15u,
				TAG_OUTPUT + (batch & 1), 0, 0);
		}
	}
	/* before a window reads the floats back */
	dma_wait((1 << TAG_OUTPUT) | (1 << (TAG_OUTPUT + 1)));
}

/* ---------- mixing (dsound_ps3.c's) */

/* mixes from the voice's position until count output frames are done or the position passes the packet's last frame
(held, not interpolated into what follows); the floats come a window at a time. Returns the output frames done */
static uint32_t mix_run(spu_mixer_voice *voice, const spu_mixer_packet *packet, struct mix_state *state,
	float *out, uint32_t count)
{
	uint32_t index = voice->index, done = 0, frames = packet->frames, channels = voice->channels;
	uint32_t step_whole = state->step_whole;
	float fraction = voice->fraction, left = state->left, right = state->right;
	float step_fraction = state->step_fraction, ramp_left = state->ramp_left, ramp_right = state->ramp_right;

	while (done < count && index < frames)
	{
		/* the frames the rest of the chunk can read: the position moves at most step_whole + 1 a frame */
		uint32_t first = index, want = (count - done) * (step_whole + 1) + 2, end, limit;
		const float *samples;

		if (want > WINDOW_FLOATS / channels)
			want = WINDOW_FLOATS / channels;
		end = frames - first < want ? frames : first + want;
		samples = fetch(window, packet->samples + first * channels * 4, (end - first) * channels * 4);
		/* index + 1 must be in the window too, unless index is the packet's last frame: then its read lands in the
		buffer's slop, and the select drops it */
		limit = end < frames ? end - 1 : frames;
		if (channels == 1)
		{
			while (done < count && index < limit)
			{
				float a = samples[index - first];
				float next = samples[index + 1 - first];
				float b = index + 1 < frames ? next : a;
				float sample = a + (b - a) * fraction;
				uint32_t wrap;

				out[0] += sample * left;
				out[1] += sample * right;
				out += 2;
				left += ramp_left;
				right += ramp_right;
				done++;
				index += step_whole;
				fraction += step_fraction;
				wrap = fraction >= 1.0f;
				fraction = wrap ? fraction - 1.0f : fraction;
				index += wrap;
			}
		}
		else
		{
			while (done < count && index < limit)
			{
				const float *a = samples + (index - first) * 2;
				float next_left = a[2], next_right = a[3];
				float b_left = index + 1 < frames ? next_left : a[0];
				float b_right = index + 1 < frames ? next_right : a[1];
				uint32_t wrap;

				out[0] += (a[0] + (b_left - a[0]) * fraction) * left;
				out[1] += (a[1] + (b_right - a[1]) * fraction) * right;
				out += 2;
				left += ramp_left;
				right += ramp_right;
				done++;
				index += step_whole;
				fraction += step_fraction;
				wrap = fraction >= 1.0f;
				fraction = wrap ? fraction - 1.0f : fraction;
				index += wrap;
			}
		}
	}
	voice->index = index;
	voice->fraction = fraction;
	state->left = left;
	state->right = right;
	return done;
}

static void mix_voice(spu_mixer_voice *voice, uint32_t count)
{
	struct mix_state state;
	uint32_t packet = 0, done = 0;

	state.step_whole = voice->step_whole;
	state.step_fraction = voice->step_fraction;
	state.left = voice->left;
	state.right = voice->right;
	state.ramp_left = voice->ramp_left;
	state.ramp_right = voice->ramp_right;
	voice->decoded = 0;
	if (voice->channels != 1 && voice->channels != 2)
		return;
	if (voice->packet_count > SPU_MIXER_PACKETS)
		voice->packet_count = SPU_MIXER_PACKETS;
	while (packet < voice->packet_count && done < count)
	{
		spu_mixer_packet *entry = &voice->packets[packet];

		if (!(entry->flags & SPU_MIXER_DECODED))
		{
			if (entry->samples)
			{
				uint32_t start = spu_read_decrementer();

				echo.stage = _stage_decode;
				decode_packet(voice, entry);
				echo.decode_ticks += start - spu_read_decrementer();
				echo.stage = _stage_mix;
			}
			entry->flags |= SPU_MIXER_DECODED;
			voice->decoded++;
			echo.packets_decoded++;
		}
		done += mix_run(voice, entry, &state, output + done * 2, count - done);
		if (voice->index >= entry->frames)
		{
			voice->index -= entry->frames;
			entry->flags |= SPU_MIXER_FINISHED;
			packet++;
		}
	}
}

/* ---------- jobs */

static void run_job(uint32_t sequence)
{
	uint32_t start = spu_read_decrementer(), voice_count, frames, first;

	echo.stage = _stage_header;
	mfc_get(&header, job_ea + offsetof(spu_mixer_job, header), sizeof(header), TAG_JOB, 0, 0);
	dma_wait(1 << TAG_JOB);
	voice_count = header.voice_count < SPU_MIXER_VOICES ? header.voice_count : SPU_MIXER_VOICES;
	frames = header.frames < SPU_MIXER_FRAMES ? header.frames : SPU_MIXER_FRAMES;
	memset(output, 0, sizeof(output));
	for (first = 0; first < voice_count; first += VOICE_BATCH)
	{
		uint32_t count = voice_count - first < VOICE_BATCH ? voice_count - first : VOICE_BATCH, index;
		uint64_t ea = job_ea + offsetof(spu_mixer_job, voices) + first * sizeof(spu_mixer_voice);

		echo.stage = _stage_voices;
		mfc_get(voices, ea, count * sizeof(spu_mixer_voice), TAG_JOB, 0, 0);
		dma_wait(1 << TAG_JOB);
		echo.stage = _stage_mix;
		for (index = 0; index < count; index++)
			mix_voice(&voices[index], frames);
		echo.stage = _stage_results;
		mfc_put(voices, ea, count * sizeof(spu_mixer_voice), TAG_JOB, 0, 0);
		dma_wait(1 << TAG_JOB);
	}
	mfc_put(output, job_ea + offsetof(spu_mixer_job, output), sizeof(output), TAG_JOB, 0, 0);
	echo.sequence = sequence;
	echo.jobs++;
	echo.busy_ticks += start - spu_read_decrementer();
	/* fenced: the PPE never sees the sequence before the results */
	mfc_putf(&echo, echo_ea, sizeof(echo), TAG_JOB, 0, 0);
	dma_wait(1 << TAG_JOB);
	echo.stage = _stage_event;
	if (spu_thread_send_event(SPU_MIXER_EVENT_PORT, sequence & 0xFFFFFF, 0) != 0)
		echo.event_errors++;
	echo.stage = _stage_idle;
}

int main(uint64_t bell_address, uint64_t echo_address, uint64_t job_address)
{
	uint32_t done = 0;

	bell_ea = bell_address;
	echo_ea = echo_address;
	job_ea = job_address;
	adpcm_tables_initialize();
	echo.state = SPU_MIXER_RUNNING;
	echo.ls_address = (uint32_t)&echo;
	put_echo();
	spu_write_event_mask(MFC_LLR_LOST_EVENT | MFC_DECREMENTER_EVENT);
	spu_write_event_ack(MFC_LLR_LOST_EVENT | MFC_DECREMENTER_EVENT);
	spu_write_decrementer(DECREMENTER_TICKS);
	for (;;)
	{
		uint32_t events;

		/* the check after each reservation closes the race with a store that lands before it */
		mfc_getllar((void *)&bell, bell_ea, 0, 0);
		mfc_read_atomic_status();
		if (bell.quit)
			break;
		if (bell.sequence != done)
		{
			done = bell.sequence;
			run_job(done);
			continue;
		}
		events = spu_read_event_status();
		spu_write_event_ack(events);
		if (events & MFC_LLR_LOST_EVENT)
			echo.wakes++;
		if (events & MFC_DECREMENTER_EVENT)
		{
			echo.timeouts++;
			spu_write_decrementer(DECREMENTER_TICKS);
		}
	}
	echo.state = SPU_MIXER_EXITED;
	put_echo();
	spu_thread_exit(0);
	return 0;
}
