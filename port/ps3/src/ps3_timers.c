/* The timer build (ps3_timers.h): every 300 frames, the game thread's time by phase, in ms a frame, and the
RSX's time by part from its timestamps. */

#include "ps3_timers.h"
#include "ps3_system.h"

#include <string.h>

#include <sys/systime.h>

#define TIMER_FRAMES 300

uint64_t ps3_timers[_ps3_timer_count];
uint32_t ps3_timer_game_ticks;
uint64_t ps3_rsx_timers[_ps3_timer_count];
uint64_t ps3_rsx_frame_nanoseconds;
uint32_t ps3_rsx_frames;

static void timers_log(uint32_t frames, uint64_t elapsed)
{
	double frequency = (double)sysGetTimebaseFrequency(), scale = 1000.0 / frequency / frames;
	double rsx_scale = ps3_rsx_frames ? 1.0e-6 / ps3_rsx_frames : 0.0;
	double t[_ps3_timer_count], r[_ps3_timer_count], frame = elapsed * scale;
	int index;

	for (index = 0; index < _ps3_timer_count; index++)
	{
		t[index] = ps3_timers[index] * scale;
		r[index] = ps3_rsx_timers[index] * rsx_scale;
	}
	ps3_log_printf("[time] %u frames, %.1f fps, %.1f ms a frame: game ticks %.1f (%.2f a frame, %.1f ms each), "
		"render %.1f, throttle wait %.1f, present %.1f, rest %.1f\n", (unsigned)frames, 1000.0 / frame, frame,
		t[_ps3_timer_ticks], (double)ps3_timer_game_ticks / frames,
		ps3_timer_game_ticks ? t[_ps3_timer_ticks] * frames / ps3_timer_game_ticks : 0.0, t[_ps3_timer_render],
		t[_ps3_timer_throttle], t[_ps3_timer_present],
		frame - t[_ps3_timer_ticks] - t[_ps3_timer_render] - t[_ps3_timer_throttle] - t[_ps3_timer_present]);
	ps3_log_printf("[time] render: sound %.1f; windows: visibility %.1f, objects %.1f, lightmaps %.1f, lights %.1f, "
		"diffuse %.1f, specular %.1f, effects %.1f, interface %.1f, end %.1f\n", t[_ps3_timer_sound],
		t[_ps3_timer_window_visibility], t[_ps3_timer_window_objects], t[_ps3_timer_window_lightmaps],
		t[_ps3_timer_window_lights], t[_ps3_timer_window_diffuse], t[_ps3_timer_window_specular],
		t[_ps3_timer_window_effects], t[_ps3_timer_window_interface], t[_ps3_timer_window_end]);
	ps3_log_printf("[time] Direct3D draws: state %.1f (textures %.1f: hash %.1f, uploads %.1f; colours %.1f), "
		"vertices %.1f, indices %.1f; waits: vblank %.1f, flip %.1f, RSX %.1f\n", t[_ps3_timer_draw_state],
		t[_ps3_timer_draw_textures], t[_ps3_timer_texture_hash], t[_ps3_timer_texture_upload],
		t[_ps3_timer_draw_colours], t[_ps3_timer_draw_vertices], t[_ps3_timer_draw_indices],
		t[_ps3_timer_vblank_wait], t[_ps3_timer_flip_wait], t[_ps3_timer_rsx_wait]);
	if (!ps3_rsx_frames)
		return;
	/* each part: the RSX's time from the split before to its own; the frame's from its first split to its last */
	ps3_log_printf("[time] RSX in %u frames, ms a frame: %.1f; before the windows %.1f, visibility %.1f, objects %.1f, "
		"lightmaps %.1f, lights %.1f, diffuse %.1f, specular %.1f, effects %.1f, interface %.1f, end %.1f, "
		"after the windows %.1f, present %.1f\n", (unsigned)ps3_rsx_frames, ps3_rsx_frame_nanoseconds * rsx_scale,
		r[_ps3_timer_rsx_before], r[_ps3_timer_window_visibility], r[_ps3_timer_window_objects],
		r[_ps3_timer_window_lightmaps], r[_ps3_timer_window_lights], r[_ps3_timer_window_diffuse],
		r[_ps3_timer_window_specular], r[_ps3_timer_window_effects], r[_ps3_timer_window_interface],
		r[_ps3_timer_window_end], r[_ps3_timer_rsx_after], r[_ps3_timer_present]);
}

void ps3_timers_frame(void)
{
	static uint64_t window_start;
	static uint32_t frames;
	uint64_t now = ps3_timer_now();

	if (window_start && ++frames < TIMER_FRAMES)
		return;
	if (window_start)
		timers_log(frames, now - window_start);
	memset(ps3_timers, 0, sizeof(ps3_timers));
	memset(ps3_rsx_timers, 0, sizeof(ps3_rsx_timers));
	ps3_rsx_frame_nanoseconds = 0;
	ps3_rsx_frames = 0;
	ps3_timer_game_ticks = 0;
	frames = 0;
	/* after the log, which is not a phase */
	window_start = ps3_timer_now();
}
