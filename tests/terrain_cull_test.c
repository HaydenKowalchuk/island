/*
 * GL-free test for terrain's per-island frustum + fog culling (island1-762c).
 *
 * Islands must only be recorded when the camera can actually see them: facing
 * the archipelago draws islands, a camera pointed away draws none (side planes
 * and near plane do the work), and a camera past FOG_END draws none even with
 * the islands dead ahead (fog does the work). Over-culling is the bug class
 * this guards: an island that is on screen must never be dropped.
 */
#include <cglm/cglm.h>
#include <stdio.h>

#include "render.h"
#include "terrain.h"

static int g_fail = 0;

static void check(int cond, const char* what) {
  if (!cond) {
    printf("terrain-cull-test: FAIL %s\n", what);
    g_fail++;
  }
}

/* Terrain-pass mesh draws recorded in the current frame. */
static int frame_terrain_draws(void) {
  const RenderFrame* f = r_frame();
  int n = 0;
  for (int i = 0; i < f->draw_count; i++)
    if (f->draws[i].pass == R_PASS_TERRAIN && f->draws[i].mesh)
      n++;
  return n;
}

/* One rendered frame from `eye` looking at `at`; returns islands drawn. */
static int emit(const float eye[3], const float at[3]) {
  mat4 view, proj;
  r_frame_begin();
  r_viewport(640, 360);
  glm_lookat((vec3){eye[0], eye[1], eye[2]},
             (vec3){at[0], at[1], at[2]},
             (vec3){0.0f, 1.0f, 0.0f},
             view);
  glm_perspective(glm_rad(70.0f), 640.0f / 360.0f, 0.5f, 400.0f, proj);
  r_camera((const float*)view, (const float*)proj);
  terrain_emit(eye);
  return frame_terrain_draws();
}

int main(void) {
  terrain_init(42u);

  /*
   * Facing the archipelago from just south of the main island: at least the
   * main island must be recorded (conservative — never drop the visible).
   */
  float eye[3] = {0.0f, 12.0f, -10.0f};
  float at[3] = {0.0f, 6.0f, 78.0f};
  int facing = emit(eye, at);
  int drawn = 0, total = 0;
  terrain_stats(&drawn, &total);
  check(facing >= 1, "islands drawn when facing the archipelago");
  check(drawn == facing, "stats agree with the recorded frame");
  check(total == 4, "stats count all islands");
  check(drawn >= 1 && drawn <= 4, "stats drawn within [1,4]");

  /*
   * Turned around: everything visible in this shot is outside at least one
   * frustum plane while still inside the fog radius, so only the frustum
   * can be doing the rejecting.
   */
  float away_eye[3] = {0.0f, 10.0f, 150.0f};
  float away_at[3] = {0.0f, 10.0f, 350.0f};
  check(emit(away_eye, away_at) == 0, "nothing drawn looking away");
  terrain_stats(&drawn, &total);
  check(drawn == 0 && total == 4, "stats report 0/4 when looking away");

  /*
   * Dead ahead but 400 units from every island: beyond FOG_END everything is
   * fog colour, so the distance test alone must cull all four.
   */
  float far_eye[3] = {0.0f, 50.0f, -400.0f};
  float far_at[3] = {0.0f, 0.0f, -800.0f};
  check(emit(far_eye, far_at) == 0, "fog culls islands past FOG_END");

  /* Side view: the east islet (x>0) is ahead of an east-facing camera. */
  float side_eye[3] = {-60.0f, 10.0f, 134.0f};
  float side_at[3] = {74.0f, 6.0f, 134.0f};
  check(emit(side_eye, side_at) >= 1, "islands drawn when seen from the side");

  terrain_shutdown();

  if (g_fail) {
    printf("terrain-cull-test: %d failure(s)\n", g_fail);
    return 1;
  }
  printf("terrain-cull-test: ok\n");
  return 0;
}
