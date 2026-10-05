/* Audio output: one 48 kHz stereo float port, fed from a ring by a thread. */

#ifndef __HALO_PS3_AUDIO_H
#define __HALO_PS3_AUDIO_H

#define PS3_AUDIO_RATE 48000

int ps3_audio_init(void);
void ps3_audio_shutdown(void);

unsigned ps3_audio_queued_frames(void);
/* queues interleaved stereo frames from -1 to 1; returns the frames taken */
unsigned ps3_audio_write(const float *frames, unsigned count);

/* blocks played as silence because the ring was empty, or only part full */
void ps3_audio_stats(unsigned *silent_blocks, unsigned *partial_blocks);

#endif
