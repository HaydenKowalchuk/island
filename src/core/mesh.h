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

#endif
