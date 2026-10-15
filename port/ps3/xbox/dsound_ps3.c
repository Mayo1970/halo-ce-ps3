/* DirectSound on the PS3 audio port (src/ps3_audio.c), after port/linux/src/dsound_sdl.c: a mixer thread keeps
the port's ring 43 ms ahead; packets complete on the game's thread (DirectSoundDoWork, Flush), never the mixer's. */

#include "ps3_audio.h"
#include "port_config.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define OUTPUT_RATE PS3_AUDIO_RATE
#define OUTPUT_CHANNELS 2
#define MAXIMUM_STREAM_PACKETS 64
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
	/* decoded at Process: interleaved floats, the source's channel count */
	float *samples;
	unsigned long frames;
	/* played out by the mixer, not yet completed */
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
	struct mixer_voice voice;
	struct voice_packet packets[MAXIMUM_STREAM_PACKETS];
	unsigned long packet_head;
	unsigned long packet_count;
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

/* the per-chunk resampling step and gain ramp of one voice */
struct mix_state
{
	unsigned long step_whole;
	float step_fraction;
	float left, right;
	float ramp_left, ramp_right;
};

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
static unsigned long packets_decoded;

/* (DirectSoundCreate comes first, on the main thread) */
static void mixer_enter(void)
{
	if (!mixer_lock)
		mixer_lock = ps3_host_lock_create(FALSE);
	/* uncontended, without the wait records of ps3_host_lock_enter: the game sets voices hundreds of times a frame */
	if (!ps3_host_lock_try_enter(mixer_lock))
		ps3_host_lock_enter(mixer_lock);
}

static void mixer_leave(void)
{
	ps3_host_lock_leave(mixer_lock);
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

/* Xbox ADPCM, as dsound_sdl.c reads it: per block a 4-byte header per channel (little-endian predictor, step
index), then 4-byte groups of eight nibbles, low nibble first, alternating between channels; 64 samples */
static float *decode_adpcm(const unsigned char *source, unsigned long size, unsigned long channels,
	unsigned long *frame_count)
{
	unsigned long block_bytes = XBOX_ADPCM_BLOCK_BYTES * channels;
	unsigned long blocks = size / block_bytes;
	float *samples = malloc((blocks ? blocks : 1) * XBOX_ADPCM_BLOCK_SAMPLES * channels * sizeof(float));
	unsigned long block, channel;

	if (!samples)
	{
		*frame_count = 0;
		return NULL;
	}
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
	*frame_count = blocks * XBOX_ADPCM_BLOCK_SAMPLES;
	return samples;
}

/* 16-bit PCM keeps the Xbox's little-endian order in memory */
static float *decode_pcm(const unsigned char *source, unsigned long size, unsigned long channels,
	unsigned long *frame_count)
{
	unsigned long frames = size / (2 * channels);
	float *samples = malloc((frames ? frames : 1) * channels * sizeof(float));
	unsigned long sample;

	if (!samples)
	{
		*frame_count = 0;
		return NULL;
	}
	for (sample = 0; sample < frames * channels; sample++)
		samples[sample] = (float)(short)(source[sample * 2] | source[sample * 2 + 1] << 8) * (1.0f / 32768.0f);
	*frame_count = frames;
	return samples;
}

static float *decode(const struct mixer_voice *voice, const void *source, unsigned long size,
	unsigned long *frame_count)
{
	if (!source || !size)
	{
		*frame_count = 0;
		return NULL;
	}
	return voice->adpcm ?
		decode_adpcm(source, size, voice->channels, frame_count) :
		decode_pcm(source, size, voice->channels, frame_count);
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

/* ---------- mixing (the mixer thread, with the lock held) */

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

static struct voice_packet *stream_first_unfinished(struct ps3_stream *stream)
{
	unsigned long position;

	for (position = 0; position < stream->packet_count; position++)
	{
		struct voice_packet *packet = &stream->packets[(stream->packet_head + position) % MAXIMUM_STREAM_PACKETS];

		if (!packet->finished)
			return packet;
	}
	return NULL;
}

static void mix_stream(struct ps3_stream *stream, float *output, unsigned long count)
{
	struct mixer_voice *voice = &stream->voice;
	struct voice_packet *packet = stream_first_unfinished(stream);
	struct mix_state state;
	unsigned long done = 0;

	if (!packet || !voice->sample_rate)
		return;
	mix_begin(voice, count, &state);
	while (packet && done < count)
	{
		done += mix_run(voice, packet->samples, packet->frames, &state, output + done * OUTPUT_CHANNELS, count - done);
		if (voice->index >= packet->frames)
		{
			voice->index -= packet->frames;
			packet->finished = TRUE;
			packet = stream_first_unfinished(stream);
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

static void mix(float *output, unsigned long count)
{
	struct ps3_stream *stream;
	struct ps3_buffer *buffer;
	unsigned long sample;

	memset(output, 0, count * OUTPUT_CHANNELS * sizeof(float));
	mixer_enter();
	for (stream = streams; stream; stream = stream->next)
		mix_stream(stream, output, count);
	for (buffer = buffers; buffer; buffer = buffer->next)
		mix_buffer(buffer, output, count);
	mixer_leave();
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
	unsigned long chunks, microseconds, maximum, playing_streams = 0, playing_buffers = 0;
	unsigned silent = 0, partial = 0;
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
	mixer_leave();
	chunks = mixer_chunks;
	microseconds = mixer_microseconds;
	maximum = mixer_maximum_microseconds;
	mixer_chunks = 0;
	mixer_microseconds = 0;
	mixer_maximum_microseconds = 0;
	if (audio_port)
		ps3_audio_stats(&silent, &partial);
	platform_log("DirectSound: %lu streams and %lu buffers playing, %lu packets decoded; mixer %lu us a chunk "
		"(max %lu) of %d; port blocks %u silent, %u partial", playing_streams, playing_buffers, packets_decoded,
		chunks ? microseconds / chunks : 0, maximum, MIX_CHUNK_MICROSECONDS, silent - last_silent,
		partial - last_partial);
	packets_decoded = 0;
	last_silent = silent;
	last_partial = partial;
}

/* ---------- completion */

/* gives back the head packet; with the lock held, which the game's callback runs without */
static void stream_complete_head(struct ps3_stream *stream, DWORD status, DWORD completed_size)
{
	struct voice_packet *entry = &stream->packets[stream->packet_head];
	XMEDIAPACKET packet = entry->packet;

	free(entry->samples);
	entry->samples = NULL;
	entry->finished = FALSE;
	stream->packet_head = (stream->packet_head + 1) % MAXIMUM_STREAM_PACKETS;
	stream->packet_count--;
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

	mixer_enter();
	for (stream = streams; stream; stream = stream->next)
	{
		while (stream->packet_count && stream->packets[stream->packet_head].finished)
			stream_complete_head(stream, XMEDIAPACKET_STATUS_SUCCESS, stream->packets[stream->packet_head].packet.dwMaxSize);
	}
	mixer_leave();
}

/* ---------- the stream interface */

static ULONG STDMETHODCALLTYPE stream_add_reference(IDirectSoundStream *object)
{
	struct ps3_stream *stream = (struct ps3_stream *)object;
	ULONG count;

	mixer_enter();
	count = ++stream->reference_count;
	mixer_leave();
	return count;
}

static HRESULT STDMETHODCALLTYPE stream_flush(IDirectSoundStream *object)
{
	struct ps3_stream *stream = (struct ps3_stream *)object;

	mixer_enter();
	while (stream->packet_count)
	{
		struct voice_packet *head = &stream->packets[stream->packet_head];

		stream_complete_head(stream, head->finished ? XMEDIAPACKET_STATUS_SUCCESS : XMEDIAPACKET_STATUS_FLUSHED,
			head->finished ? head->packet.dwMaxSize : 0);
	}
	stream->voice.index = 0;
	stream->voice.fraction = 0.0f;
	mixer_leave();
	return S_OK;
}

static ULONG STDMETHODCALLTYPE stream_release(IDirectSoundStream *object)
{
	struct ps3_stream *stream = (struct ps3_stream *)object;
	struct ps3_stream **link;
	ULONG count;

	mixer_enter();
	count = --stream->reference_count;
	mixer_leave();
	if (count)
		return count;
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
	mixer_leave();
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

	mixer_enter();
	*status = stream->packet_count < MAXIMUM_STREAM_PACKETS ? XMO_STATUSF_ACCEPT_INPUT_DATA : 0;
	mixer_leave();
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_process(IDirectSoundStream *object, LPCXMEDIAPACKET input, LPCXMEDIAPACKET output)
{
	struct ps3_stream *stream = (struct ps3_stream *)object;
	struct voice_packet *entry;
	unsigned long frames = 0;
	float *samples;

	(void)output;
	if (!input)
		return E_INVALIDARG;
	/* outside the lock, on the game's thread */
	samples = decode(&stream->voice, input->pvBuffer, input->dwMaxSize, &frames);
	mixer_enter();
	if (stream->packet_count == MAXIMUM_STREAM_PACKETS)
	{
		mixer_leave();
		free(samples);
		return E_OUTOFMEMORY;
	}
	entry = &stream->packets[(stream->packet_head + stream->packet_count) % MAXIMUM_STREAM_PACKETS];
	entry->packet = *input;
	entry->samples = samples;
	entry->frames = samples ? frames : 0;
	entry->finished = FALSE;
	if (input->pdwStatus)
		*input->pdwStatus = XMEDIAPACKET_STATUS_PENDING;
	if (input->pdwCompletedSize)
		*input->pdwCompletedSize = 0;
	if (!stream->packet_count)
	{
		/* a stream that ran dry starts over */
		stream->voice.index = 0;
		stream->voice.fraction = 0.0f;
		stream->voice.gains_valid = FALSE;
	}
	stream->packet_count++;
	packets_decoded++;
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

HRESULT WINAPI IDirectSound_SetDistanceFactor(LPDIRECTSOUND sound, FLOAT factor, DWORD apply)
{
	(void)sound;
	(void)apply;
	mixer_enter();
	listener.distance_factor = factor > 0.0f ? factor : 1.0f;
	mixer_leave();
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetRolloffFactor(LPDIRECTSOUND sound, FLOAT factor, DWORD apply)
{
	(void)sound;
	(void)apply;
	mixer_enter();
	listener.rolloff_factor = factor >= 0.0f ? factor : 1.0f;
	mixer_leave();
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetPosition(LPDIRECTSOUND sound, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)sound;
	(void)apply;
	mixer_enter();
	listener.position[0] = x;
	listener.position[1] = y;
	listener.position[2] = z;
	mixer_leave();
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
	(void)sound;
	(void)apply;
	mixer_enter();
	listener.front[0] = x_front;
	listener.front[1] = y_front;
	listener.front[2] = z_front;
	listener.top[0] = x_top;
	listener.top[1] = y_top;
	listener.top[2] = z_top;
	normalize3(listener.front);
	normalize3(listener.top);
	mixer_leave();
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
	unsigned long active;

	mixer_enter();
	active = record->packet_count != 0;
	mixer_leave();
	return active;
}

#define STREAM_SETTER(body) \
	struct ps3_stream *record = (struct ps3_stream *)stream; \
	mixer_enter(); \
	body; \
	mixer_leave(); \
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

	mixer_enter();
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
	mixer_leave();
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
	mixer_enter();
	record->voice.frequency = (DWORD)frequency;
	mixer_leave();
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_SetVolume(LPDIRECTSOUNDBUFFER buffer, LONG volume)
{
	struct ps3_buffer *record = (struct ps3_buffer *)buffer;
	float gain = gain_from_millibels(volume);

	mixer_enter();
	record->voice.volume = gain;
	mixer_leave();
	return DS_OK;
}
