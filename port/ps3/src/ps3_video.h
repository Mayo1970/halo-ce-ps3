/* RSX display: three flip buffers at vsync on the TV's mode (720p preferred). */

#ifndef __HALO_PS3_VIDEO_H
#define __HALO_PS3_VIDEO_H

#include <rsx/rsx.h>

int ps3_video_init(void);
void ps3_video_shutdown(void);

gcmContextData *ps3_video_context(void);
void ps3_video_size(unsigned *width, unsigned *height);
/* the display mode's shape: nonzero for 16:9 */
int ps3_video_widescreen(void);

/* waits for a free buffer and makes it the render target */
void ps3_video_begin_frame(void);
void ps3_video_clear(unsigned argb);
/* queues the flip of the current buffer at the next vsync */
void ps3_video_end_frame(void);

void ps3_video_stats(unsigned *flips_queued, unsigned *flips_completed, unsigned *wait_timeouts);
/* (the G8 diagnostic) logs the frames the RSX has taken, flipped and finished */
void ps3_video_flip_report(void);
/* the RSX local memory of the display and depth buffers */
unsigned ps3_video_memory_bytes(void);

/* the display thread (ps3_main.c) stops drawing: the caller owns the RSX until it releases it */
void ps3_display_acquire(void);
void ps3_display_release(void);

#endif
