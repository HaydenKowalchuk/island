#ifndef ISLAND_CORE_LOD_H
#define ISLAND_CORE_LOD_H

/*
 * Tiny distance-LOD helper (island1-1927). Props are drawn at full detail when
 * close, at a simplified mesh in the middle band, and dropped once far enough
 * that distance fog hides the cut (LOD_CULL_DIST sits inside FOG_END).
 *
 * Distances are squared and measured on the XZ plane from the active camera.
 */

typedef enum { LOD_NEAR = 0, LOD_MID, LOD_FAR } LodLevel;

#define LOD_NEAR_DIST 80.0f  /* full detail within this radius */
#define LOD_CULL_DIST 200.0f /* beyond this, cull (fog is ~89% at FOG_END 220) */

static inline LodLevel lod_level(float dx, float dz) {
    float d2 = dx * dx + dz * dz;
    if (d2 <= LOD_NEAR_DIST * LOD_NEAR_DIST)
        return LOD_NEAR;
    if (d2 <= LOD_CULL_DIST * LOD_CULL_DIST)
        return LOD_MID;
    return LOD_FAR;
}

#endif
