/* RSX display: three flip buffers at vsync on the TV's mode (720p preferred). */

#ifndef __HALO_PS3_VIDEO_H
#define __HALO_PS3_VIDEO_H

#include <rsx/rsx.h>

int ps3_video_init(void);
void ps3_video_shutdown(void);

gcmContextData *ps3_video_context(void);
void ps3_video_size(unsigned *width, unsigned *height);

/* waits for a free buffer and makes it the render target */
void ps3_video_begin_frame(void);
void ps3_video_clear(unsigned argb);
/* queues the flip of the current buffer at the next vsync */
void ps3_video_end_frame(void);

void ps3_video_stats(unsigned *flips_queued, unsigned *flips_completed, unsigned *wait_timeouts);

#endif
