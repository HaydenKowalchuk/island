#ifndef ISLAND_CORE_FRUSTUM_H
#define ISLAND_CORE_FRUSTUM_H

/*
 * View-frustum plane tests shared by every world-space pass (island1-762c).
 *
 * The GE (PSP) still runs the vertex transform for every submitted polygon even
 * when it ends up clipped, so the only culling that saves real work is skipping
 * draws on the CPU before they are recorded. Terrain and the ocean grid both
 * extract from clip = proj * view and test with these helpers; all tests are
 * conservative — they may keep something invisible, never drop something
 * visible.
 */

/* Plane in world/clip-consistent space: a*x + b*y + c*z + d, unit normal. */
typedef struct {
  float a, b, c, d;
} Plane;

/*
 * Extract the six clip-space planes from clip = proj * view (Gribb-Hartmann).
 * cglm matrices are column-major, so matrix row r is (m[r], m[4+r], ...).
 * Order: 0 left, 1 right, 2 bottom, 3 top, 4 near, 5 far.
 */
void frustum_extract(const float* clip, Plane planes[6]);

/* Conservative AABB test: reject only when the box is fully outside a plane. */
int frustum_aabb_visible(const Plane planes[6], float x0, float y0, float z0, float x1, float y1, float z1);

/* Conservative sphere test (used by prop LOD); `radius` must be >= 0. */
int frustum_sphere_visible(const Plane planes[6], float x, float y, float z, float radius);

/*
 * Tighter test for a flat quad: its four corners in world space,
 * (x0,z0,y00) (x0,z1,y01) (x1,z1,y11) (x1,z0,y10). The quad hull is inside its
 * own AABB, so this rejects rim cells the box test must keep, and it still
 * keeps anything whose corner is inside any plane.
 */
int frustum_quad_visible(const Plane planes[6], float x0, float z0, float x1, float z1, float y00, float y01, float y11, float y10);

#endif
