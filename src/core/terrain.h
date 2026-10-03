#ifndef ISLAND_CORE_TERRAIN_H
#define ISLAND_CORE_TERRAIN_H

#include <cglm/cglm.h>

/*
 * Seeded low-poly island terrain (island1-39q6 / island1-xf8m).
 *
 * The island is a grid of faceted quads: deterministic value-noise fbm for the
 * hill plus a radial falloff that drops the rim below sea level, so a beach ring
 * forms around the shore. Height sampling is exposed for gameplay and camera.
 */

/* Builds the island heightmap and mesh. Call once after render_init(). */
void terrain_init(unsigned int seed);

/* Frees the island mesh. */
void terrain_shutdown(void);

/* Terrain height in metres at a world position; negative/`-1000` means open sea. */
float terrain_height_at(float x, float z);

/* Unit surface normal at a world position (central differences). */
void terrain_normal_at(float x, float z, vec3 out);

/* Records the island mesh as the terrain pass of the current frame. */
void terrain_emit(void);

#endif
