#ifndef ISLAND_CORE_TERRAIN_H
#define ISLAND_CORE_TERRAIN_H

#include <cglm/cglm.h>

/*
 * Seeded low-poly islands (island1-39q6 / island1-xf8m).
 *
 * Each island is a radial (polar) mesh: a triangle fan from the summit out to
 * concentric rings, so triangles follow the round shoreline instead of filling a
 * bounding square. Height is deterministic value-noise fbm times a cubic radial
 * falloff with a wavy coast, so a beach ring forms and the rim sinks below sea
 * level. Sampling is analytic (the same function the mesh is built from).
 */

/* Builds the islands' meshes. Call once after render_init(). */
void terrain_init(unsigned int seed);

/* Frees the island meshes. */
void terrain_shutdown(void);

/* Terrain height in metres at a world position; `-1000` means open sea. */
float terrain_height_at(float x, float z);

/* Unit surface normal at a world position (central differences). */
void terrain_normal_at(float x, float z, vec3 out);

/* Records the island meshes as the terrain pass of the current frame. */
void terrain_emit(void);

#endif
