#include "game.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <cglm/cglm.h>

#include "camera.h"
#include "chase_cam.h"
#include "input.h"
#include "mesh.h"
#include "render.h"
#include "texture.h"

/*
 * Core game state and rules. This file must stay platform-agnostic: it may only
 * touch the RenderAPI (render.h) and the input contract (input.h), never
 * GL/GLFW.
 */

static FlyCam s_cam;
static int s_view_w = 1280;
static int s_view_h = 720;

/* Sim time, advanced on the fixed tick and read by the renderer for waves/bob.
 */
static float s_time = 0.0f;

/* The hover car, built once and drawn with a per-frame transform. */
static Mesh s_car;

/* A textured parcel, bobbing ahead of the car to exercise the texture path. */
static Mesh s_parcel;

/*
 * Chase camera plus a minimal hover-car state it follows. This driving model is
 * a stand-in for island1-9n0n (real physics) and island1-9gio (bob/bank); it
 * exists so the camera has something to track and the feel can be tuned.
 */
static ChaseCam s_chase;

typedef struct {
  vec3 pos;      /* world position; y = water level */
  float heading; /* radians; forward = (sin(h), 0, cos(h)) */
  float speed;   /* units/s (signed) */
  float speed_kmh;
  float bank; /* radians; leans into the turn */
} CarState;

static CarState s_veh;

/*
 * Wake trail: the actual recent path of the car in the XZ plane. Recording
 * world-space points (instead of resampling a fake rope every frame) keeps the
 * ribbon still; the y value is rebuilt each frame from the wave field.
 */
#define TRAIL_MAX 64
#define TRAIL_STEP 1.1f
#define WAKE_LIFT 0.14f

static float s_trail[TRAIL_MAX * 2];
static int s_trail_count = 0;

static void trail_clear(void) { s_trail_count = 0; }

static void trail_push(const float *pos) {
  if (s_trail_count > 0) {
    float dx = pos[0] - s_trail[0];
    float dz = pos[2] - s_trail[1];
    if (dx * dx + dz * dz < TRAIL_STEP * TRAIL_STEP)
      return;
  }

  int last = (s_trail_count < TRAIL_MAX) ? s_trail_count : TRAIL_MAX - 1;
  for (int i = last; i > 0; i--) {
    s_trail[i * 2 + 0] = s_trail[(i - 1) * 2 + 0];
    s_trail[i * 2 + 1] = s_trail[(i - 1) * 2 + 1];
  }
  s_trail[0] = pos[0];
  s_trail[1] = pos[2];
  if (s_trail_count < TRAIL_MAX)
    s_trail_count++;
}

typedef enum { CAM_CHASE, CAM_FLY } CamMode;
static CamMode s_cam_mode = CAM_CHASE;

/* Dev overlay (F3) and ocean cull counters shown in it. */
static int s_debug = 1;
static int s_ocean_drawn = 0;
static int s_ocean_total = 0;

static void build_car(void) {
  const float hull[4] = {0.42f, 0.06f, 0.07f, 1.0f};
  const float body[4] = {0.72f, 0.11f, 0.10f, 1.0f};
  const float glass[4] = {0.06f, 0.08f, 0.12f, 1.0f};

  mesh_make(&s_car);
  mesh_add_box(&s_car, (const float[]){0.0f, 0.34f, 0.0f},
               (const float[]){0.70f, 0.16f, 1.75f}, hull);
  mesh_add_box(&s_car, (const float[]){0.0f, 0.60f, 0.0f},
               (const float[]){0.90f, 0.20f, 2.00f}, body);
  mesh_add_box(&s_car, (const float[]){0.0f, 0.94f, -0.20f},
               (const float[]){0.60f, 0.22f, 0.80f}, glass);
  mesh_add_box(&s_car, (const float[]){0.0f, 0.58f, 1.62f},
               (const float[]){0.55f, 0.12f, 0.38f}, body);
}

/* White cube textured with the parcel tile, so the texture path is visible. */
static void build_parcel(void) {
  const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};

  mesh_make(&s_parcel);
  mesh_add_box(&s_parcel, (const float[]){0.0f, 0.0f, 0.0f},
               (const float[]){0.35f, 0.35f, 0.35f}, white);
  s_parcel.texture = texture_get("textures/parcel_32.png");
}

void game_init(void) {
  cam_init(&s_cam);

  glm_vec3_zero(s_veh.pos);
  s_veh.heading = 0.0f;
  s_veh.speed = 0.0f;
  s_veh.speed_kmh = 0.0f;
  s_veh.bank = 0.0f;
  trail_clear();
  chase_cam_init(&s_chase, s_veh.pos, s_veh.heading);
  s_cam_mode = CAM_CHASE;

  build_car();
  build_parcel();
}

void game_shutdown(void) {
  mesh_destroy(&s_car);
  mesh_destroy(&s_parcel);
}

void game_resize(int width, int height) {
  if (width > 0 && height > 0) {
    s_view_w = width;
    s_view_h = height;
  }
}

/* Minimal arcade driving; replaced by island1-9n0n later. */
#define CAR_ACCEL 24.0f /* must exceed DRAG * MAX_SPEED to reach top end */
#define CAR_DRAG 0.55f
#define CAR_MAX_SPEED 34.0f /* ~122 km/h */
#define CAR_REVERSE 10.0f
#define CAR_TURN_RATE 1.9f
#define CAR_BANK_MAX 0.61f /* radians, ~35 deg aggressive lean (island1-9gio)  \
                            */
#define CAR_BANK_RATE 4.0f

static void drive_car(float dt) {
  float throttle = input_axis_throttle();
  float steer = input_axis_steer();

  s_veh.speed += throttle * CAR_ACCEL * dt;
  s_veh.speed -= s_veh.speed * CAR_DRAG * dt;

  if (s_veh.speed > CAR_MAX_SPEED)
    s_veh.speed = CAR_MAX_SPEED;
  if (s_veh.speed < -CAR_REVERSE)
    s_veh.speed = -CAR_REVERSE;

  /* Steering bites harder with speed, so the car feels planted when still. */
  float speed01 = fabsf(s_veh.speed) / CAR_MAX_SPEED;
  s_veh.heading -= steer * CAR_TURN_RATE * dt * (0.35f + 0.65f * speed01);

  s_veh.pos[0] += sinf(s_veh.heading) * s_veh.speed * dt;
  s_veh.pos[2] += cosf(s_veh.heading) * s_veh.speed * dt;
  s_veh.speed_kmh = fabsf(s_veh.speed) * 3.6f;

  /* Ease the bank so the car leans into the turn instead of snapping. */
  float bank_target = steer * CAR_BANK_MAX * speed01;
  s_veh.bank += (bank_target - s_veh.bank) * (1.0f - expf(-CAR_BANK_RATE * dt));

  trail_push(s_veh.pos);

  if (input_pressed(ACT_RESET)) {
    glm_vec3_zero(s_veh.pos);
    s_veh.heading = 0.0f;
    s_veh.speed = 0.0f;
    s_veh.speed_kmh = 0.0f;
    s_veh.bank = 0.0f;
    trail_clear();
    chase_cam_init(&s_chase, s_veh.pos, s_veh.heading);
  }
}

void game_tick(float dt) {
  const InputState *in = input_state();

  s_time += dt;

  if (input_pressed(ACT_TOGGLE_CAM)) {
    s_cam_mode = (s_cam_mode == CAM_CHASE) ? CAM_FLY : CAM_CHASE;
    if (s_cam_mode == CAM_FLY)
      cam_reset(&s_cam);
    else
      chase_cam_init(&s_chase, s_veh.pos, s_veh.heading);
  }

  if (input_pressed(ACT_TOGGLE_DEBUG))
    s_debug = !s_debug;

  if (s_cam_mode == CAM_FLY) {
    /* Free-fly dev camera: look and speed adjustments stay in the sim. */
    if (input_held(ACT_LOOK))
      cam_rotate(&s_cam, in->mouse_dx, in->mouse_dy);

    if (input_held(ACT_BOOST))
      s_cam.speed = 40.0f;
    else if (input_held(ACT_SLOW))
      s_cam.speed = 3.0f;
    else
      s_cam.speed = 10.0f;

    if (input_pressed(ACT_RESET))
      cam_reset(&s_cam);

    cam_move(&s_cam, (int)input_axis_throttle(), (int)input_axis_steer(), 0,
             dt);
  } else {
    drive_car(dt);
  }
}

/* ---- Frame recording -------------------------------------------------- */

/* Sky: a normalised window-space quad with a vertical gradient. */
static void emit_sky(void) {
  r_pass(R_PASS_SKY);
  int base = r_vertex(0.0f, 0.0f, 0.0f, SKY_COLOR_R, SKY_COLOR_G, SKY_COLOR_B,
                      1.0f, 0.0f, 0.0f);
  r_vertex(1.0f, 0.0f, 0.0f, SKY_COLOR_R, SKY_COLOR_G, SKY_COLOR_B, 1.0f, 0.0f,
           0.0f);
  r_vertex(1.0f, 1.0f, 0.0f, SKY_ZENITH_R, SKY_ZENITH_G, SKY_ZENITH_B, 1.0f,
           1.0f, 0.0f);
  r_vertex(0.0f, 1.0f, 0.0f, SKY_ZENITH_R, SKY_ZENITH_G, SKY_ZENITH_B, 1.0f,
           1.0f, 0.0f);

  RDraw d = {0};
  d.prim = R_QUADS;
  d.first = base;
  d.count = 4;
  d.screen_space = 1;
  d.tint[0] = d.tint[1] = d.tint[2] = d.tint[3] = 1.0f;
  r_draw(&d);
}

/* ---- Ocean ------------------------------------------------------------ */

#define OCEAN_N 25
#define OCEAN_R 260.0f
#define OCEAN_SKEW 0.55f
#define OCEAN_WAVE_AMP 0.16f

/*
 * Smooth world-space shade variation. It must be continuous (not a hash), or
 * the camera-centred grid would pop every time a cell centre crossed an
 * integer as it slid along with the eye.
 */
static float ocean_tint(float x, float z) {
  return 0.5f + 0.28f * sinf(x * 0.045f + z * 0.028f) +
         0.22f * sinf(z * 0.071f - x * 0.019f + 1.7f);
}

static float wave_height(float x, float z, float t) {
  return OCEAN_WAVE_AMP * sinf(x * 0.12f + t * 1.1f) +
         0.10f * cosf(z * 0.17f - t * 0.8f);
}

/*
 * A regular NxN topology mapped through a cubic: cells stay small near the eye
 * and grow towards the fogged rim. Because it is one continuous grid the
 * neighbours share vertices, so there are no T-junction cracks.
 */
static float ocean_axis(int k) {
  float u = ((float)k / (float)(OCEAN_N - 1)) * 2.0f - 1.0f; /* -1..1 */
  return OCEAN_R * (OCEAN_SKEW * u + (1.0f - OCEAN_SKEW) * u * u * u);
}

/* ---- Frustum culling -------------------------------------------------- */

typedef struct {
  float a, b, c, d;
} Plane;

static void frustum_plane(const float row3[4], const float row[4], float sign,
                          Plane *p) {
  p->a = row3[0] + sign * row[0];
  p->b = row3[1] + sign * row[1];
  p->c = row3[2] + sign * row[2];
  p->d = row3[3] + sign * row[3];

  float len = sqrtf(p->a * p->a + p->b * p->b + p->c * p->c);
  if (len > 1e-6f) {
    p->a /= len;
    p->b /= len;
    p->c /= len;
    p->d /= len;
  }
}

/*
 * Extract the six clip-space planes from clip = proj * view (Gribb-Hartmann).
 * cglm matrices are column-major, so matrix row r is (m[r], m[4+r], ...).
 */
static void extract_frustum(const float *clip, Plane planes[6]) {
  const float row0[4] = {clip[0], clip[4], clip[8], clip[12]};
  const float row1[4] = {clip[1], clip[5], clip[9], clip[13]};
  const float row2[4] = {clip[2], clip[6], clip[10], clip[14]};
  const float row3[4] = {clip[3], clip[7], clip[11], clip[15]};

  frustum_plane(row3, row0, +1.0f, &planes[0]); /* left   */
  frustum_plane(row3, row0, -1.0f, &planes[1]); /* right  */
  frustum_plane(row3, row1, +1.0f, &planes[2]); /* bottom */
  frustum_plane(row3, row1, -1.0f, &planes[3]); /* top    */
  frustum_plane(row3, row2, +1.0f, &planes[4]); /* near   */
  frustum_plane(row3, row2, -1.0f, &planes[5]); /* far    */
}

/* Conservative AABB test: reject only when the box is fully outside a plane. */
static int aabb_in_frustum(const Plane planes[6], float x0, float y0, float z0,
                           float x1, float y1, float z1) {
  for (int i = 0; i < 6; i++) {
    const Plane *p = &planes[i];
    float px = (p->a >= 0.0f) ? x1 : x0;
    float py = (p->b >= 0.0f) ? y1 : y0;
    float pz = (p->c >= 0.0f) ? z1 : z0;
    if (p->a * px + p->b * py + p->c * pz + p->d < 0.0f)
      return 0;
  }
  return 1;
}

/*
 * One batched draw of a camera-centred, distance-graded water grid. Cells near
 * the eye stay small (wave detail) and grow towards the fogged rim, so a fixed
 * 24x24 topology covers the whole visible sea for a fraction of the old vertex
 * count. Each cell is flat-shaded (its four corners share a colour) which gives
 * the faceted PS1 read; heights come from a smooth world-space function so
 * neighbouring cells line up without cracks. Cells outside the view frustum are
 * skipped.
 */
static void emit_ocean(float t) {
  mat4 proj, view, clip;
  Plane planes[6];
  memcpy(proj, r_frame()->proj, sizeof(proj));
  memcpy(view, r_frame()->view, sizeof(view));
  glm_mat4_mul(proj, view, clip);
  extract_frustum((const float *)clip, planes);

  /* view is column-major; eye = -R^T * t. */
  const float *vf = (const float *)view;
  const float ex =
      -(vf[0] * vf[12] + vf[1] * vf[13] + vf[2] * vf[14]);
  const float ez =
      -(vf[8] * vf[12] + vf[9] * vf[13] + vf[10] * vf[14]);

  float ax[OCEAN_N], az[OCEAN_N];
  for (int k = 0; k < OCEAN_N; k++) {
    float o = ocean_axis(k);
    ax[k] = ex + o;
    az[k] = ez + o;
  }

  r_pass(R_PASS_OCEAN);
  s_ocean_drawn = 0;
  s_ocean_total = 0;

  int first = -1;
  int count = 0;

  for (int i = 0; i < OCEAN_N - 1; i++) {
    float z0 = az[i];
    float z1 = az[i + 1];

    for (int j = 0; j < OCEAN_N - 1; j++) {
      float x0 = ax[j];
      float x1 = ax[j + 1];

      s_ocean_total++;

      /* Cells whose nearest point is past the fog are invisible: skip them. */
      float cdx = fabsf((x0 + x1) * 0.5f - ex) - (x1 - x0) * 0.5f;
      float cdz = fabsf((z0 + z1) * 0.5f - ez) - (z1 - z0) * 0.5f;
      if (cdx < 0.0f)
        cdx = 0.0f;
      if (cdz < 0.0f)
        cdz = 0.0f;
      if (cdx * cdx + cdz * cdz > FOG_END * FOG_END)
        continue;

      float y00 = wave_height(x0, z0, t);
      float y10 = wave_height(x1, z0, t);
      float y11 = wave_height(x1, z1, t);
      float y01 = wave_height(x0, z1, t);

      float ymin = fminf(fminf(y00, y10), fminf(y11, y01));
      float ymax = fmaxf(fmaxf(y00, y10), fmaxf(y11, y01));
      if (!aabb_in_frustum(planes, x0, ymin, z0, x1, ymax, z1))
        continue;
      s_ocean_drawn++;

      float h = ocean_tint((x0 + x1) * 0.5f, (z0 + z1) * 0.5f);
      float r = 0.05f + 0.10f * h;
      float g = 0.30f + 0.22f * h;
      float b = 0.42f + 0.20f * h;

      int v = r_vertex(x0, y00, z0, r, g, b, 1.0f, 0.0f, 0.0f);
      if (first < 0)
        first = v;
      r_vertex(x1, y10, z0, r, g, b, 1.0f, 0.0f, 0.0f);
      r_vertex(x1, y11, z1, r, g, b, 1.0f, 0.0f, 0.0f);
      r_vertex(x0, y01, z1, r, g, b, 1.0f, 0.0f, 0.0f);
      count += 4;
    }
  }

  RDraw d = {0};
  d.prim = R_QUADS;
  d.first = first;
  d.count = count;
  d.fog = 1;
  d.depth_write = 1;
  d.cull = R_CULL_NONE;
  d.tint[0] = d.tint[1] = d.tint[2] = d.tint[3] = 1.0f;
  r_draw(&d);
}

/* ---- Hover car -------------------------------------------------------- */

/*
 * A red car/boat hybrid as a handful of boxes (no wheels, per the art brief):
 * a dark under-hull, a red body, a dark wedge cabin and a low nose. It bobs and
 * rocks gently so it reads as floating on the water.
 */
static void emit_car(float t) {
  r_pass(R_PASS_VEHICLE);
  const float bob = 0.05f * sinf(t * 1.6f);
  const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};

  mat4 world;
  glm_mat4_identity(world);
  glm_translate(world, (vec3){s_veh.pos[0], s_veh.pos[1] + bob, s_veh.pos[2]});
  glm_rotate(world, s_veh.heading, (vec3){0.0f, 1.0f, 0.0f});
  glm_rotate(world, s_veh.bank, (vec3){0.0f, 0.0f, 1.0f});

  r_mesh(&s_car, (const float *)world, white);
}

/* A textured parcel spinning slightly ahead of the car. */
static void emit_parcel(float t) {
  r_pass(R_PASS_PROPS);
  const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float fwd[3] = {sinf(s_veh.heading), 0.0f, cosf(s_veh.heading)};

  mat4 world;
  glm_mat4_identity(world);
  glm_translate(world, (vec3){
                           s_veh.pos[0] + fwd[0] * 5.0f,
                           1.5f + 0.25f * sinf(t * 2.0f),
                           s_veh.pos[2] + fwd[2] * 5.0f,
                       });
  glm_rotate(world, t * 0.8f, (vec3){0.0f, 1.0f, 0.0f});

  r_mesh(&s_parcel, (const float *)world, white);
}

/* Foam wake following the car's recorded path, floating just above the waves.
 */
static void emit_wake(float t) {
  r_pass(R_PASS_PARTICLES);
  if (s_trail_count < 2)
    return;

  float pts[TRAIL_MAX * 3];
  for (int i = 0; i < s_trail_count; i++) {
    float x = s_trail[i * 2 + 0];
    float z = s_trail[i * 2 + 1];
    pts[i * 3 + 0] = x;
    pts[i * 3 + 1] = wave_height(x, z, t) + WAKE_LIFT;
    pts[i * 3 + 2] = z;
  }

  const float foam[4] = {0.90f, 0.97f, 1.0f, 0.55f};
  r_ribbon(pts, s_trail_count, 1.6f, foam);
}

void game_render(float frame_dt) {
  r_frame_begin();
  r_viewport(s_view_w, s_view_h);

  const float fog_color[4] = {SKY_COLOR_R, SKY_COLOR_G, SKY_COLOR_B, 1.0f};
  r_fog(1, fog_color, FOG_START, FOG_END);

  if (s_cam_mode == CAM_CHASE) {
    chase_cam_update(&s_chase, s_veh.pos, s_veh.heading, s_veh.speed_kmh,
                     s_view_w, s_view_h, frame_dt);
    r_camera((const float *)s_chase.view, (const float *)s_chase.proj);
  } else {
    cam_update(&s_cam, s_view_w, s_view_h);
    r_camera((const float *)s_cam.view, (const float *)s_cam.proj);
  }

  emit_sky();
  emit_ocean(s_time);
  emit_wake(s_time);
  emit_car(s_time);
  emit_parcel(s_time);

  const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  char buf[48];

  r_pass(R_PASS_HUD);
  snprintf(buf, sizeof(buf), "%d KMH", (int)(s_veh.speed_kmh + 0.5f));
  r_text_2d(12, 10, 2.0f, s_cam_mode == CAM_CHASE ? "CHASE" : "FLY", white);
  r_text_2d(12, 32, 1.5f, buf, white);
  r_text_2d(12, 54, 1.0f, "WASD DRIVE  F FLY  F3 STATS", white);

  /* Snapshot after the scene/HUD so the overlay itself is not counted. */
  const RenderFrame *fr = r_frame();
  int pass_v[R_PASS_COUNT];
  int pass_d[R_PASS_COUNT];
  memcpy(pass_v, fr->pass_verts, sizeof(pass_v));
  memcpy(pass_d, fr->pass_draws, sizeof(pass_d));

  if (s_debug) {
    int x = s_view_w - 160;
    int y = 10;

    snprintf(buf, sizeof(buf), "DRAWS %d  VERTS %d", fr->stat_draws,
             fr->stat_verts);
    r_text_2d(x, y, 1.0f, buf, white);
    y += 12;

    for (int i = 0; i < R_PASS_COUNT; i++) {
      if (pass_d[i] == 0)
        continue;
      snprintf(buf, sizeof(buf), "%-7s %5dv %2dd", r_pass_name((RPass)i),
               pass_v[i], pass_d[i]);
      r_text_2d(x, y, 1.0f, buf, white);
      y += 12;
    }

    snprintf(buf, sizeof(buf), "OCEAN %d/%d CELLS", s_ocean_drawn,
             s_ocean_total);
    r_text_2d(x, y, 1.0f, buf, white);
  }
}
