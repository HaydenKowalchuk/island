/*
 * GL-free test for the shared frustum helpers (island1-762c).
 *
 * Culling must only ever drop geometry that is *fully* outside one of the six
 * planes: the PSP's GE still transforms every submitted vertex, so skipping
 * whole island meshes / ocean cells on the CPU is the only culling that saves
 * real work — but a wrong test pops visible geometry on screen.
 *
 * Checks, against a real perspective*view extraction:
 *   - a box in front of the camera survives; boxes outside each of the six
 *     planes (left, right, bottom, top, near, far) are rejected;
 *   - conservative behaviour: a box straddling a side plane survives;
 *   - the sphere test matches for centre/behind cases;
 *   - the quad test is *strictly tighter* than the AABB test (the case the
 *     ocean grid culling is built on).
 */
#include "frustum.h"

#include <cglm/cglm.h>
#include <math.h>
#include <stdio.h>

static int g_fail = 0;

static void check(int cond, const char* what) {
  if (!cond) {
    printf("frustum-test: FAIL %s\n", what);
    g_fail++;
  }
}

int main(void) {
  /* Camera at (0,0,10) looking down -Z, the island layout's facing. */
  mat4 proj, view, vp;
  Plane p[6];
  glm_perspective(glm_rad(70.0f), 16.0f / 9.0f, 0.5f, 100.0f, proj);
  glm_lookat((vec3){0.0f, 0.0f, 10.0f}, (vec3){0.0f, 0.0f, 0.0f}, (vec3){0.0f, 1.0f, 0.0f}, view);
  glm_mat4_mul(proj, view, vp);
  frustum_extract((const float*)vp, p);

  check(frustum_aabb_visible(p, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f),
        "box in front is visible");
  check(!frustum_aabb_visible(p, -1.0f, -1.0f, 14.0f, 1.0f, 1.0f, 16.0f),
        "box behind the camera (near plane)");
  check(!frustum_aabb_visible(p, -1.0f, -1.0f, -95.0f, 1.0f, 1.0f, -92.0f),
        "box past the far plane");
  check(!frustum_aabb_visible(p, -200.0f, -1.0f, -10.0f, -190.0f, 1.0f, -5.0f),
        "box far to the left (left plane)");
  check(!frustum_aabb_visible(p, 190.0f, -1.0f, -10.0f, 200.0f, 1.0f, -5.0f),
        "box far to the right (right plane)");
  check(!frustum_aabb_visible(p, -1.0f, 100.0f, -10.0f, 1.0f, 110.0f, -5.0f),
        "box far above (top plane)");
  check(!frustum_aabb_visible(p, -1.0f, -110.0f, -10.0f, 1.0f, -100.0f, -5.0f),
        "box far below (bottom plane)");

  /* Conservative: straddling the left edge must stay visible. */
  check(frustum_aabb_visible(p, -40.0f, -1.0f, -12.0f, 0.0f, 1.0f, -5.0f),
        "box straddling the left edge stays visible");

  check(frustum_sphere_visible(p, 0.0f, 0.0f, 0.0f, 1.0f),
        "sphere in front is visible");
  check(!frustum_sphere_visible(p, 0.0f, 0.0f, 15.0f, 1.0f),
        "sphere behind the camera");
  check(frustum_sphere_visible(p, 0.0f, 0.0f, -50.0f, 200.0f),
        "sphere huge enough to contain the camera stays visible");

  /* Quad hull: reject only when every corner is outside the same plane. */
  check(frustum_quad_visible(p, -1.0f, -1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f),
        "small quad at the origin is visible");
  check(!frustum_quad_visible(p, -60.0f, -2.0f, -50.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
        "quad fully left of the frustum is rejected");

  /*
   * Tightness: with a synthetic plane (inside is -x + 0.5y + 1 >= 0), the
   * quad's four corners at y=0, x in [2,3] are all outside, while the AABB's
   * min/max span (y up to 2) still contains an inside corner. This is the
   * exact shape of the ocean rim: the box keeps a cell the quad hull drops.
   */
  Plane q[6] = {
      {-1.0f, 0.5f, 0.0f, 1.0f},
      {0.0f, 0.0f, 0.0f, 1.0f},
      {0.0f, 0.0f, 0.0f, 1.0f},
      {0.0f, 0.0f, 0.0f, 1.0f},
      {0.0f, 0.0f, 0.0f, 1.0f},
      {0.0f, 0.0f, 0.0f, 1.0f},
  };
  check(frustum_aabb_visible(q, 2.0f, 0.0f, 0.0f, 3.0f, 2.0f, 1.0f),
        "AABB keeps the conservative box");
  check(!frustum_quad_visible(q, 2.0f, 0.0f, 3.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f),
        "quad hull rejects what the AABB kept");

  if (g_fail) {
    printf("frustum-test: %d failure(s)\n", g_fail);
    return 1;
  }
  printf("frustum-test: ok\n");
  return 0;
}
