#ifndef ISLAND_CORE_LOD_H
#define ISLAND_CORE_LOD_H

#include "render.h" /* FOG_END: the fog IS the far cull */

/*
 * Distance LOD unified with the fog (island1-288p). Three drawable levels
 * cover the visible range; past FOG_END an entity is exactly fog colour (==
 * sky), so the fog itself is the cull — there is no independent cull
 * distance, and retuning the fog moves the bands and the cull together.
 *
 * Distances are squared and measured on the XZ plane from the active camera.
 */

typedef enum {
  LOD_NEAR = 0, /* full detail */
  LOD_MID,      /* simplified mesh */
  LOD_FAR,      /* farthest drawable band: fog is 77-100% here */
  LOD_CULL,     /* beyond full fog: invisible, do not record */
  LOD_LEVEL_COUNT
} LodLevel;

#define LOD_NEAR_DIST 80.0f
#define LOD_MID_DIST 140.0f

static inline LodLevel lod_level(float dx, float dz) {
  float d2 = dx * dx + dz * dz;
  if (d2 <= LOD_NEAR_DIST * LOD_NEAR_DIST)
    return LOD_NEAR;
  if (d2 <= LOD_MID_DIST * LOD_MID_DIST)
    return LOD_MID;
  if (d2 <= FOG_END * FOG_END)
    return LOD_FAR;
  return LOD_CULL;
}

#endif
