#ifndef ISLAND_CORE_MESH_H
#define ISLAND_CORE_MESH_H

#include "render.h"

/*
 * Tiny geometry helpers that emit through the RenderAPI. They exist so content
 * code (ocean, car, buildings) can describe faceted low-poly shapes without
 * hand-writing vertex/draw pairs. Flat-shading comes from per-vertex colour, so
 * no runtime lighting is needed.
 */

/* One flat-shaded quad (4 positions, CCW as seen from the front). */
void mesh_quad(const float a[3], const float b[3], const float c[3], const float d[3],
               const float color[4], int fog, int lit, RCull cull);

/*
 * Axis-aligned box centred at `center` with `half` extents. The six faces get
 * slightly different shades (top brightest, bottom darkest) so the facets read.
 * Drawn double-sided because content is small and winding is easy to get wrong.
 */
void mesh_box(const float center[3], const float half[3], const float color[4],
              int fog, int lit);

/*
 * A reusable mesh: interleaved vertices plus 16-bit triangle indices, kept in
 * client memory (no VBOs, no GL). Build once at startup, draw many times with
 * r_mesh() and a world transform. `texture` is a backend texture handle; 0 means
 * untextured (vertex colours only).
 */
typedef struct Mesh {
    RVertex* verts;
    int vert_count;
    unsigned short* indices;
    int index_count;
    int texture;
} Mesh;

void mesh_make(Mesh* m);
void mesh_destroy(Mesh* m);

/* Append one quad (4 positions, CCW) as two triangles with a flat colour. */
void mesh_add_quad(Mesh* m, const float a[3], const float b[3], const float c[3],
                   const float d[3], const float color[4]);

/* Append an axis-aligned box, outward-wound and per-face shaded. */
void mesh_add_box(Mesh* m, const float center[3], const float half[3],
                  const float color[4]);

#endif
