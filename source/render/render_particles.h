/* RENDER_PARTICLES.H: header included in hcex build. */

#ifndef __RENDER_PARTICLES_H
#define __RENDER_PARTICLES_H
#pragma once

/* ---------- headers */

#include "cseries/cseries.h"

/* ---------- prototypes/RENDER_PARTICLES.C */

void render_particles(
	void);
#ifdef HALO_PS3
/* port: the window's particles gathered, their sprites built on the render SPU; render_particles draws them */
void render_particles_spu_begin(
	void);
/* port: a map's sprite tables for the SPU go (particles_dispose_from_old_map) */
void render_particles_spu_dispose(
	void);
#endif

/* ---------- globals */

extern boolean render_particles_enabled;

#endif // __RENDER_PARTICLES_H
