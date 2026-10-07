#include "game.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <cglm/cglm.h>

#include "camera.h"
#include "car.h"
#include "chase_cam.h"
#include "dialog.h"
#include "frustum.h"
#include "input.h"
#include "lod.h"
#include "mesh.h"
#include "pi.h"
#include "render.h"
#include "terrain.h"
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
 * Chase camera plus the hover car it follows. The driving model lives in car.c
 * (island1-9n0n); s_bank is presentation-only lean (island1-9gio will extend
 * this into bob/pitch).
 */
static ChaseCam s_chase;
static CarState s_veh;
static float s_bank = 0.0f; /* radians; leans into the turn */

/* Active camera position, sampled each render for prop LOD (island1-1927). */
static float s_cam_pos[3];

/*
 * Player mode (island1-zku4): the player is either driving the hover car or
 * walking on foot. On foot the player snaps to the island surface and cannot
 * walk into deep water; F (ACT_VEHICLE) enters/exits next to the car.
 */
typedef enum { MODE_DRIVE, MODE_FOOT } PlayerMode;

#define FOOT_SPEED 4.5f
#define FOOT_TURN 6.0f
#define FOOT_WADE -0.8f /* ground below this is too deep to walk */
#define VEHICLE_REACH 3.5f
#define VEHICLE_EXIT_OFFSET 2.6f

typedef struct {
  vec3 pos;
  float heading;
  float speed_kmh;
  float walk_phase;
} PlayerState;

static PlayerMode s_mode = MODE_DRIVE;
static PlayerState s_player;
static Mesh s_player_mesh;

/* ---- Delivery mission loop (island1-pkcj -> island1-draq) -------------- */

/*
 * Live sandbox loop: the package waits at the depot on the main island; every
 * request picks a random recipient house on one of the islands, so the run
 * keeps moving. A coloured beacon marks the active objective (cyan = pick up,
 * green = deliver). Full conversation / on-foot handover is island1-38qy.
 */
#define DEPOT_X 8.0f
#define DEPOT_Z 82.0f
#define QUEST_PICKUP_RANGE 4.5f
#define QUEST_DELIVER_RANGE 5.0f
#define QUEST_DONE_TIME 1.4f
#define TOAST_TIME 1.8f
#define CAR_START_X 0.0f
#define CAR_START_Z 0.0f
#define HOUSE_COUNT 4
#define NPC_LOOK_RANGE 30.0f

/* Recipient house + the resident standing at its doorway. */
typedef struct {
  vec3 base;     /* house origin on the ground (door faces +z of the mesh) */
  vec3 resident; /* resident world position, in front of the door */
  float facing;  /* resting heading (outward, away from the door) */
  float look;    /* eased heading actually drawn */
} House;

static const float HOUSE_SITES[HOUSE_COUNT][3] = {
    {-14.0f, 70.0f, 0.0f},  /* main island */
    {74.0f, 134.0f, 0.0f},  /* east islet */
    {-66.0f, 126.0f, 0.0f}, /* west islet */
    {20.0f, -74.0f, 0.0f},  /* southern islet */
};
static const float HOUSE_TINT[HOUSE_COUNT][4] = {
    {1.0f, 0.90f, 0.80f, 1.0f},
    {0.80f, 1.0f, 0.85f, 1.0f},
    {1.0f, 1.0f, 0.75f, 1.0f},
    {0.90f, 0.85f, 1.0f, 1.0f},
};

static House s_houses[HOUSE_COUNT];
static vec3 s_depot;

static GameQuestPhase s_quest = GAME_QUEST_REQUEST;
static float s_quest_timer = 0.0f;
static float s_toast_timer = 0.0f;
static int s_delivered = 0;
static int s_target_house = -1;
static vec3 s_pkg_pos;
static unsigned int s_rng = 0x1234567u;
static Mesh s_pack;
static Mesh s_house;
static Mesh s_house_lod; /* simplified body-only house for the mid LOD band */
static Mesh s_depot_mesh;

static unsigned int rng_next(void) {
  s_rng = s_rng * 1664525u + 1013904223u;
  return s_rng >> 8;
}

/* Land height snapped to sea level off-island. */
static float ground_y(float x, float z) {
  float g = terrain_height_at(x, z);
  return (g > 0.0f) ? g : 0.0f;
}

static void quest_place_package(void) {
  s_pkg_pos[0] = s_depot[0];
  s_pkg_pos[1] = s_depot[1] + 0.55f;
  s_pkg_pos[2] = s_depot[2];
}

static void quest_new_request(void) {
  int idx;
  do {
    idx = (int)(rng_next() % (unsigned)HOUSE_COUNT);
  } while (idx == s_target_house);
  s_target_house = idx;
  s_quest = GAME_QUEST_REQUEST;
  quest_place_package();
}

/* Position of whatever the player is currently controlling. */
static const float *active_pos(void) {
  return (s_mode == MODE_FOOT) ? s_player.pos : s_veh.pos;
}

/* The active objective: the depot while requesting, else the target resident.
 */
static const float *active_target(void) {
  return (s_quest == GAME_QUEST_CARRY) ? s_houses[s_target_house].resident
                                       : s_depot;
}

/*
 * Conversation lines for the pickup/delivery beats. The resident names and the
 * data-driven tables arrive with the NPC conversation system (island1-9fm3);
 * these static buffers outlive dialog_start(), which only stores pointers.
 */
static char s_dialog_lines[DIALOG_MAX_LINES][DIALOG_MAX_CHARS];

static void quest_tick(float dt) {
  const float *ap = active_pos();

  if (s_quest == GAME_QUEST_REQUEST) {
    float dx = ap[0] - s_depot[0];
    float dz = ap[2] - s_depot[2];
    if (dx * dx + dz * dz <= QUEST_PICKUP_RANGE * QUEST_PICKUP_RANGE &&
        !dialog_active() && input_pressed(ACT_INTERACT)) {
      s_quest = GAME_QUEST_CARRY;
      /* Hand-off chatter (island1-a2xf textbox; wording is 9fm3's to replace).
       */
      snprintf(s_dialog_lines[0], sizeof(s_dialog_lines[0]),
               "PACKAGE FOR HOUSE %d. HEAVY ONE.", s_target_house + 1);
      snprintf(s_dialog_lines[1], sizeof(s_dialog_lines[1]),
               "DELIVER IT TO THE RESIDENT, THEN HURRY BACK.");
      const char *lines[DIALOG_MAX_LINES] = {s_dialog_lines[0],
                                             s_dialog_lines[1]};
      dialog_start("DEPOT KEEPER", lines, 2);
    }
  } else if (s_quest == GAME_QUEST_CARRY) {
    const float *rp = s_houses[s_target_house].resident;
    float dx = ap[0] - rp[0];
    float dz = ap[2] - rp[2];
    if (dx * dx + dz * dz <= QUEST_DELIVER_RANGE * QUEST_DELIVER_RANGE &&
        !dialog_active() && input_pressed(ACT_INTERACT)) {
      s_quest = GAME_QUEST_DONE;
      s_quest_timer = QUEST_DONE_TIME;
      s_toast_timer = TOAST_TIME;
      s_delivered++;
      snprintf(s_dialog_lines[0], sizeof(s_dialog_lines[0]),
               "MY PACKAGE! FINALLY!");
      snprintf(s_dialog_lines[1], sizeof(s_dialog_lines[1]),
               "THE ISLAND THANKS YOU, COURIER.");
      const char *lines[DIALOG_MAX_LINES] = {s_dialog_lines[0],
                                             s_dialog_lines[1]};
      dialog_start("RESIDENT", lines, 2);
    }
  } else { /* GAME_QUEST_DONE */
    s_quest_timer -= dt;
    if (s_quest_timer <= 0.0f)
      quest_new_request();
  }

  if (s_toast_timer > 0.0f)
    s_toast_timer -= dt;

  /* Residents ease around to watch whatever the player is controlling. */
  for (int i = 0; i < HOUSE_COUNT; i++) {
    House *h = &s_houses[i];
    float dx = ap[0] - h->resident[0];
    float dz = ap[2] - h->resident[2];
    float target = h->facing;
    if (dx * dx + dz * dz <= NPC_LOOK_RANGE * NPC_LOOK_RANGE)
      target = atan2f(dx, dz);

    float d = target - h->look;
    while (d > (float)ISLAND_PI)
      d -= 2.0f * (float)ISLAND_PI;
    while (d < -(float)ISLAND_PI)
      d += 2.0f * (float)ISLAND_PI;
    h->look += d * (1.0f - expf(-6.0f * dt));
  }
}

/*
 * Wake trail: the actual recent path of the car in the XZ plane. Recording
 * world-space points (instead of resampling a fake rope every frame) keeps the
 * ribbon still; the y value is rebuilt each frame from the wave field.
 */
#define TRAIL_MAX 20 /* 22 units of wake, ~0.8 s at the 28 m/s top speed */
#define TRAIL_STEP 1.1f
#define WAKE_LIFT 1.2f
/* The wake has to start at the stern, not under the hull: the car body spans
 * z in [-2, +2] in model space and +Z is forward, so recording the car's own
 * origin laid the ribbon across the front half of the car. */
#define WAKE_OFFSET (2.2f)

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

/* Gameplay HUD visibility (island1-fovq); toggled by game_set_hud(). */
static int s_hud = 1;

/* Dev overlay (F3) and ocean cull counters shown in it. */
static int s_debug = 0;
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
  s_parcel.texture = texture_get("textures/parcel.png");
}

/* Low-poly humanoid (island1-urkr placeholder): a few flat-shaded boxes. */
static void build_player(void) {
  const float pants[4] = {0.18f, 0.20f, 0.30f, 1.0f};
  const float shirt[4] = {0.25f, 0.45f, 0.75f, 1.0f};
  const float skin[4] = {0.85f, 0.68f, 0.52f, 1.0f};
  const float hair[4] = {0.25f, 0.18f, 0.12f, 1.0f};

  mesh_make(&s_player_mesh);
  mesh_add_box(&s_player_mesh, (const float[]){0.0f, 0.42f, 0.0f},
               (const float[]){0.20f, 0.42f, 0.16f}, pants);
  mesh_add_box(&s_player_mesh, (const float[]){0.0f, 1.08f, 0.0f},
               (const float[]){0.30f, 0.30f, 0.18f}, shirt);
  mesh_add_box(&s_player_mesh, (const float[]){0.0f, 1.58f, 0.0f},
               (const float[]){0.20f, 0.20f, 0.20f}, skin);
  mesh_add_box(&s_player_mesh, (const float[]){0.0f, 1.74f, 0.0f},
               (const float[]){0.22f, 0.08f, 0.22f}, hair);
  mesh_add_box(&s_player_mesh, (const float[]){-0.44f, 1.05f, 0.0f},
               (const float[]){0.10f, 0.35f, 0.12f}, shirt);
  mesh_add_box(&s_player_mesh, (const float[]){0.44f, 1.05f, 0.0f},
               (const float[]){0.10f, 0.35f, 0.12f}, shirt);
}

/* Small pack worn on the player's back while carrying on foot (island1-fhno).
 */
static void build_pack(void) {
  const float brown[4] = {0.45f, 0.30f, 0.16f, 1.0f};

  mesh_make(&s_pack);
  mesh_add_box(&s_pack, (const float[]){0.0f, 0.0f, 0.0f},
               (const float[]){0.26f, 0.30f, 0.14f}, brown);
}

/*
 * Recipient house (island1-bo69 placeholder): a box with a warm gable roof, a
 * dark doorway on the +z face and overhanging eaves. Door/shutters read at
 * distance; the real prefab with a doorway anchor comes later.
 */
static void build_house(void) {
  const float wall[4] = {0.78f, 0.70f, 0.52f, 1.0f};
  const float roof[4] = {0.42f, 0.24f, 0.18f, 1.0f};
  const float door[4] = {0.30f, 0.20f, 0.12f, 1.0f};

  mesh_make(&s_house);
  mesh_add_box(&s_house, (const float[]){0.0f, 1.05f, 0.0f},
               (const float[]){1.8f, 1.05f, 1.5f}, wall);

  /* Two roof slopes, wound outward. */
  mesh_add_quad(&s_house, (const float[]){-1.95f, 1.9f, 1.78f},
                (const float[]){1.95f, 1.9f, 1.78f},
                (const float[]){1.95f, 2.6f, 0.0f},
                (const float[]){-1.95f, 2.6f, 0.0f}, roof);
  mesh_add_quad(&s_house, (const float[]){1.95f, 1.9f, -1.78f},
                (const float[]){-1.95f, 1.9f, -1.78f},
                (const float[]){-1.95f, 2.6f, 0.0f},
                (const float[]){1.95f, 2.6f, 0.0f}, roof);

  /* Gable ends close the triangle under the ridge. */
  mesh_add_triangle(&s_house, (const float[]){-1.8f, 2.1f, -1.5f},
                    (const float[]){-1.8f, 2.1f, 1.5f},
                    (const float[]){-1.8f, 2.6f, 0.0f}, wall);
  mesh_add_triangle(&s_house, (const float[]){1.8f, 2.1f, 1.5f},
                    (const float[]){1.8f, 2.1f, -1.5f},
                    (const float[]){1.8f, 2.6f, 0.0f}, wall);

  mesh_add_box(&s_house, (const float[]){0.0f, 0.7f, 1.52f},
               (const float[]){0.42f, 0.7f, 0.08f}, door);

  /* Mid-distance LOD: the wall box only, no roof/door/eaves. */
  mesh_make(&s_house_lod);
  mesh_add_box(&s_house_lod, (const float[]){0.0f, 1.05f, 0.0f},
               (const float[]){1.8f, 1.05f, 1.5f}, wall);
}

/* The package depot: a low pad with a post and a bright sign (island1-draq). */
static void build_depot(void) {
  const float pad[4] = {0.72f, 0.70f, 0.62f, 1.0f};
  const float post[4] = {0.45f, 0.32f, 0.20f, 1.0f};
  const float sign[4] = {0.92f, 0.76f, 0.26f, 1.0f};

  mesh_make(&s_depot_mesh);
  mesh_add_box(&s_depot_mesh, (const float[]){0.0f, 0.06f, 0.0f},
               (const float[]){2.2f, 0.06f, 2.2f}, pad);
  mesh_add_box(&s_depot_mesh, (const float[]){1.6f, 1.1f, 0.0f},
               (const float[]){0.10f, 1.1f, 0.10f}, post);
  mesh_add_box(&s_depot_mesh, (const float[]){1.6f, 2.15f, 0.0f},
               (const float[]){0.75f, 0.45f, 0.06f}, sign);
}

static void apply_cam_framing(void);

void game_init(void) {
  cam_init(&s_cam);
  s_time = 0.0f;
  s_rng = 0x1234567u;
  s_delivered = 0;
  s_target_house = -1;
  s_toast_timer = 0.0f;

  /* Terrain must exist before anything snaps to the ground. */
  terrain_init(1337u);
  build_car();
  build_parcel();
  build_player();
  build_pack();
  build_house();
  build_depot();

  glm_vec3_zero(s_player.pos);
  s_player.heading = 0.0f;
  s_player.speed_kmh = 0.0f;
  s_player.walk_phase = 0.0f;
  s_mode = MODE_DRIVE;

  /* Start parked on the main island, facing the resident. */
  car_init(&s_veh, CAR_START_X, CAR_START_Z, 0.0f);
  s_bank = 0.0f;
  {
    float g = terrain_height_at(s_veh.pos[0], s_veh.pos[2]);
    s_veh.pos[1] = (g > 0.0f) ? g + 0.25f : 0.0f;
    s_veh.on_water = (g <= 0.0f);
  }
  trail_clear();
  apply_cam_framing();
  chase_cam_init(&s_chase, s_veh.pos, s_veh.heading);
  chase_cam_set_ground(terrain_height_at);
  s_cam_mode = CAM_CHASE;

  /* The package depot on the main island. */
  s_depot[0] = DEPOT_X;
  s_depot[2] = DEPOT_Z;
  s_depot[1] = ground_y(s_depot[0], s_depot[2]);

  /* One resident house per island; the resident stands in front of the door. */
  for (int i = 0; i < HOUSE_COUNT; i++) {
    House *h = &s_houses[i];
    h->base[0] = HOUSE_SITES[i][0];
    h->base[2] = HOUSE_SITES[i][1];
    h->base[1] = ground_y(h->base[0], h->base[2]);
    h->facing = HOUSE_SITES[i][2];
    h->resident[0] = h->base[0] + sinf(h->facing) * 3.0f;
    h->resident[2] = h->base[2] + cosf(h->facing) * 3.0f;
    h->resident[1] = ground_y(h->resident[0], h->resident[2]);
    h->look = h->facing;
  }

  quest_new_request();
}

void game_shutdown(void) {
  terrain_shutdown();
  mesh_destroy(&s_car);
  mesh_destroy(&s_parcel);
  mesh_destroy(&s_player_mesh);
  mesh_destroy(&s_pack);
  mesh_destroy(&s_house);
  mesh_destroy(&s_house_lod);
  mesh_destroy(&s_depot_mesh);
}

void game_resize(int width, int height) {
  if (width > 0 && height > 0) {
    s_view_w = width;
    s_view_h = height;
  }
}

GameMode game_mode(void) {
  return (s_mode == MODE_FOOT) ? GAME_MODE_FOOT : GAME_MODE_DRIVE;
}

const float *game_player_pos(void) { return s_player.pos; }

const float *game_vehicle_pos(void) { return s_veh.pos; }

GameQuestPhase game_quest_phase(void) { return s_quest; }

int game_quest_carried(void) { return s_quest == GAME_QUEST_CARRY; }

int game_delivered_count(void) { return s_delivered; }

const float *game_npc_pos(void) { return s_houses[s_target_house].resident; }

const float *game_package_pos(void) { return s_pkg_pos; }

const float *game_depot_pos(void) { return s_depot; }

int game_target_house(void) { return s_target_house; }

int game_beacon_active(void) { return s_quest != GAME_QUEST_DONE; }

const float *game_beacon_pos(void) { return active_target(); }

void game_place_vehicle(float x, float z, float heading) {
  car_init(&s_veh, x, z, heading);
  s_bank = 0.0f;

  float g = terrain_height_at(x, z);
  s_veh.pos[1] = (g > 0.0f) ? g + 0.25f : 0.0f;
  s_veh.on_water = (g <= 0.0f);

  trail_clear();
  chase_cam_init(&s_chase, s_veh.pos, s_veh.heading);
}

void game_set_hud(int on) { s_hud = on ? 1 : 0; }

int game_hud_enabled(void) { return s_hud; }

/* Presentation-only lean; the physics lives in car.c (island1-9n0n). */
#define CAR_BANK_MAX 0.78f /* radians, ~45 deg lean (island1-9gio) */
#define CAR_BANK_RATE 4.0f

static void drive_car(float dt) {
  /* Analog triggers feed the axes; the digital actions stay as a fallback. */
  float throttle =
      fmaxf(input_axis(AX_THROTTLE), input_held(ACT_THROTTLE) ? 1.0f : 0.0f);
  float brake =
      fmaxf(input_axis(AX_BRAKE), input_held(ACT_BRAKE) ? 1.0f : 0.0f);
  float steer = input_axis(AX_STEER);

  /* Physics reads the surface under the car before it integrates. */
  float ground = terrain_height_at(s_veh.pos[0], s_veh.pos[2]);
  car_tick(&s_veh, throttle, brake, steer, ground, dt);

  /* Hover just over land; float at sea level over water. */
  float g2 = terrain_height_at(s_veh.pos[0], s_veh.pos[2]);
  s_veh.pos[1] = (g2 > 0.0f) ? g2 + 0.25f : 0.0f;

  /* Ease the bank so the car leans into the turn instead of snapping. */
  float speed01 = (s_veh.speed_kmh / 3.6f) / CAR_MAX_FWD;
  if (speed01 > 1.0f)
    speed01 = 1.0f;
  float bank_target = steer * CAR_BANK_MAX * speed01;
  s_bank += (bank_target - s_bank) * (1.0f - expf(-CAR_BANK_RATE * dt));

  /* Record the stern rather than the car's origin, so the wake trails the hull
   * instead of starting underneath (or ahead of) it. Forward is (sin h, cos h),
   * so the stern is one WAKE_OFFSET behind. */
  float wake[3] = {s_veh.pos[0] - sinf(s_veh.heading) * WAKE_OFFSET,
                   s_veh.pos[1],
                   s_veh.pos[2] - cosf(s_veh.heading) * WAKE_OFFSET};
  trail_push(wake);

  if (input_pressed(ACT_DEV_RESET)) {
    car_init(&s_veh, 0.0f, 0.0f, 0.0f);
    s_bank = 0.0f;
    trail_clear();
    chase_cam_init(&s_chase, s_veh.pos, s_veh.heading);
  }
}

/*
 * Enter or leave the car (island1-zku4). Getting out is only allowed while the
 * car is over land; the player is placed on the first dry side of the car.
 * Getting back in requires standing within reach of the parked car.
 */
/* Closer, lower chase rig when on foot; the vehicle rig otherwise. */
static void apply_cam_framing(void) {
  if (s_mode == MODE_FOOT)
    chase_cam_set_framing(4.5f, 1.9f, 1.2f);
  else
    chase_cam_set_framing(7.0f, 3.2f, 0.8f);
}

static void toggle_vehicle(void) {
  if (s_mode == MODE_DRIVE) {
    if (terrain_height_at(s_veh.pos[0], s_veh.pos[2]) <= 0.0f)
      return; /* must be on an island to disembark */

    float right[2] = {cosf(s_veh.heading), -sinf(s_veh.heading)};
    float fwd[2] = {sinf(s_veh.heading), cosf(s_veh.heading)};
    float cand[4][2] = {
        {s_veh.pos[0] + right[0] * VEHICLE_EXIT_OFFSET,
         s_veh.pos[2] + right[1] * VEHICLE_EXIT_OFFSET},
        {s_veh.pos[0] - right[0] * VEHICLE_EXIT_OFFSET,
         s_veh.pos[2] - right[1] * VEHICLE_EXIT_OFFSET},
        {s_veh.pos[0] - fwd[0] * VEHICLE_EXIT_OFFSET,
         s_veh.pos[2] - fwd[1] * VEHICLE_EXIT_OFFSET},
        {s_veh.pos[0], s_veh.pos[2]},
    };

    for (int i = 0; i < 4; i++) {
      float g = terrain_height_at(cand[i][0], cand[i][1]);
      if (g <= FOOT_WADE && i < 3)
        continue;
      s_player.pos[0] = cand[i][0];
      s_player.pos[1] = (g > 0.0f) ? g + 0.05f : 0.0f;
      s_player.pos[2] = cand[i][1];
      s_player.heading = s_veh.heading;
      s_player.speed_kmh = 0.0f;
      s_player.walk_phase = 0.0f;
      glm_vec3_zero(s_veh.vel);
      s_veh.yaw_rate = 0.0f;
      s_veh.speed_kmh = 0.0f;
      trail_clear();
      s_mode = MODE_FOOT;
      apply_cam_framing();
      chase_cam_init(&s_chase, s_player.pos, s_player.heading);
      return;
    }
  } else {
    float dx = s_player.pos[0] - s_veh.pos[0];
    float dz = s_player.pos[2] - s_veh.pos[2];
    if (dx * dx + dz * dz > VEHICLE_REACH * VEHICLE_REACH)
      return;

    s_mode = MODE_DRIVE;
    glm_vec3_zero(s_veh.vel);
    s_veh.yaw_rate = 0.0f;
    s_veh.speed_kmh = 0.0f;
    trail_clear();
    apply_cam_framing();
    chase_cam_init(&s_chase, s_veh.pos, s_veh.heading);
  }
}

/*
 * On-foot walking (island1-7duk, minimal): W/S walk along the facing direction,
 * A/D turn. The player follows the terrain height and is blocked by deep water
 * (shallow water can be waded at sea level).
 */
static void walk_player(float dt) {
  float throttle =
      fmaxf(input_axis(AX_THROTTLE), input_held(ACT_THROTTLE) ? 1.0f : 0.0f);
  float brake =
      fmaxf(input_axis(AX_BRAKE), input_held(ACT_BRAKE) ? 1.0f : 0.0f);
  float move = throttle - brake;
  float steer = input_axis(AX_STEER);

  s_player.heading -= steer * FOOT_TURN * dt;

  float v = move * FOOT_SPEED;
  float nx = s_player.pos[0] + sinf(s_player.heading) * v * dt;
  float nz = s_player.pos[2] + cosf(s_player.heading) * v * dt;
  float g = terrain_height_at(nx, nz);

  if (g > FOOT_WADE) {
    s_player.pos[0] = nx;
    s_player.pos[2] = nz;
    s_player.pos[1] = (g > 0.0f) ? g + 0.05f : 0.0f;
    s_player.walk_phase += fabsf(move) * dt * 9.0f;
  }
  /* else: deep water ahead, stand still. */

  s_player.speed_kmh = fabsf(v) * 3.6f;
}

void game_tick(float dt) {
  s_time += dt;

  if (input_pressed(ACT_DEV_CAM)) {
    s_cam_mode = (s_cam_mode == CAM_CHASE) ? CAM_FLY : CAM_CHASE;
    if (s_cam_mode == CAM_FLY)
      cam_reset(&s_cam);
    else {
      apply_cam_framing();
      if (s_mode == MODE_FOOT)
        chase_cam_init(&s_chase, s_player.pos, s_player.heading);
      else
        chase_cam_init(&s_chase, s_veh.pos, s_veh.heading);
    }
  }

  if (input_pressed(ACT_DEV_DEBUG))
    s_debug = !s_debug;

  if (input_pressed(ACT_VEHICLE))
    toggle_vehicle();

  if (s_cam_mode == CAM_FLY) {
    /* Free-fly dev camera: look and speed adjustments stay in the sim. */
    if (input_held(ACT_LOOK))
      cam_rotate(&s_cam, input_axis(AX_LOOK_X), input_axis(AX_LOOK_Y));

    if (input_held(ACT_BOOST))
      s_cam.speed = 40.0f;
    else if (input_held(ACT_BRAKE))
      s_cam.speed = 3.0f;
    else
      s_cam.speed = 10.0f;

    if (input_pressed(ACT_DEV_RESET))
      cam_reset(&s_cam);

    float fly_throttle = (input_held(ACT_THROTTLE) ? 1.0f : 0.0f) -
                         (input_held(ACT_BRAKE) ? 1.0f : 0.0f);
    cam_move(&s_cam, (int)fly_throttle, (int)input_axis(AX_STEER), 0, dt);
  } else if (s_mode == MODE_FOOT) {
    walk_player(dt);
  } else {
    drive_car(dt);
  }

  quest_tick(dt);

  /* Conversation typewriter + E advance/skip (island1-a2xf). Runs after the
   * quest logic so the press that opened a box is consumed, not reused. */
  dialog_update(dt, input_pressed(ACT_INTERACT));
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
  /* Screen-space, but part of the scene rather than an overlay: it must sort
   * and depth-test like solid geometry so it sits behind the island. */
  d.composite = R_COMPOSITE_OPAQUE;
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

/* Frustum tests live in frustum.{h,c} so terrain and the ocean grid share one
 * extraction and one set of plane tests (island1-762c / island1-mc2v). */

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
  frustum_extract((const float *)clip, planes);

  /* view is column-major; eye = -R^T * t. */
  const float *vf = (const float *)view;
  const float ex = -(vf[0] * vf[12] + vf[1] * vf[13] + vf[2] * vf[14]);
  const float ez = -(vf[8] * vf[12] + vf[9] * vf[13] + vf[10] * vf[14]);

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

      /* Cheap reject before the wave math: the whole wave range sits inside a
       * two-unit-tall box, so this can never drop a visible cell — it just
       * keeps the side/behind cells out of the four sin/cos evals. */
      if (!frustum_aabb_visible(planes, x0, -1.0f, z0, x1, 1.0f, z1))
        continue;

      float y00 = wave_height(x0, z0, t);
      float y10 = wave_height(x1, z0, t);
      float y11 = wave_height(x1, z1, t);
      float y01 = wave_height(x0, z1, t);

      /* Tight test on the cell's four real corners: the quad hull is inside
       * its own AABB, so this rejects more rim cells at the side planes than
       * the box test would, without ever popping a cell that is on screen. */
      if (!frustum_quad_visible(planes, x0, z0, x1, z1, y00, y01, y11, y10))
        continue;
      s_ocean_drawn++;

      float h = ocean_tint((x0 + x1) * 0.5f, (z0 + z1) * 0.5f);
      float r = 0.05f + 0.10f * h;
      float g = 0.30f + 0.22f * h;
      float b = 0.42f + 0.20f * h;

      int v = r_vertex(x0, y00, z0, r, g, b, 1.0f, 0.0f, 0.0f);
      if (first < 0)
        first = v;
      /* Wound so the geometric normal points up: (x0,z0) (x0,z1) (x1,z1)
       * (x1,z0). Single-sided (cull back) — the PVR's opaque compare mislikes
       * two coincident faces at identical depth, and the desktop culls the
       * hidden face for free. */
      r_vertex(x0, y01, z1, r, g, b, 1.0f, 0.0f, 0.0f);
      r_vertex(x1, y11, z1, r, g, b, 1.0f, 0.0f, 0.0f);
      r_vertex(x1, y10, z0, r, g, b, 1.0f, 0.0f, 0.0f);
      count += 4;
    }
  }

  RDraw d = {0};
  d.prim = R_QUADS;
  d.first = first;
  d.count = count;
  d.composite = R_COMPOSITE_OPAQUE;
  d.fog = 1;
  d.depth_write = 1;
  d.cull = R_CULL_BACK;
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
  glm_rotate(world, s_bank, (vec3){0.0f, 0.0f, 1.0f});

  r_mesh(&s_car, (const float *)world, white);
}

/* The walking player (island1-zku4/urkr): humanoid mesh on the island surface.
 */
static void emit_player(float t) {
  (void)t;
  r_pass(R_PASS_VEHICLE);
  const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  const float bob = 0.05f * sinf(s_player.walk_phase);

  mat4 world;
  glm_mat4_identity(world);
  glm_translate(
      world, (vec3){s_player.pos[0], s_player.pos[1] + bob, s_player.pos[2]});
  glm_rotate(world, s_player.heading, (vec3){0.0f, 1.0f, 0.0f});

  r_mesh(&s_player_mesh, (const float *)world, white);
}

/*
 * The parcel: waiting on the depot pad (spinning, hovering), strapped to the
 * car's rear deck while driving, or hidden on foot in favour of the backpack.
 */
static void emit_parcel(float t) {
  if (s_quest == GAME_QUEST_DONE)
    return;
  if (s_quest == GAME_QUEST_CARRY && s_mode == MODE_FOOT)
    return;

  r_pass(R_PASS_PROPS);
  const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};

  mat4 world;
  glm_mat4_identity(world);

  if (s_quest == GAME_QUEST_CARRY) {
    glm_translate(world, (vec3){s_veh.pos[0], s_veh.pos[1], s_veh.pos[2]});
    glm_rotate(world, s_veh.heading, (vec3){0.0f, 1.0f, 0.0f});
    glm_translate(world, (vec3){0.0f, 1.05f, -0.45f});
    glm_rotate(world, t * 0.4f, (vec3){0.0f, 1.0f, 0.0f});
  } else {
    glm_translate(world,
                  (vec3){s_pkg_pos[0], s_pkg_pos[1] + 0.18f * sinf(t * 2.0f),
                         s_pkg_pos[2]});
    glm_rotate(world, t * 0.8f, (vec3){0.0f, 1.0f, 0.0f});
  }

  if (lod_level(world[3][0] - s_cam_pos[0], world[3][2] - s_cam_pos[2]) ==
      LOD_FAR)
    return;
  r_mesh(&s_parcel, (const float *)world, white);
}

/* All recipient houses. */
static void emit_houses(float t) {
  (void)t;
  r_pass(R_PASS_PROPS);
  const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};

  for (int i = 0; i < HOUSE_COUNT; i++) {
    mat4 world;
    glm_mat4_identity(world);
    glm_translate(world, s_houses[i].base);
    glm_rotate(world, s_houses[i].facing, (vec3){0.0f, 1.0f, 0.0f});
    LodLevel lod = lod_level(s_houses[i].base[0] - s_cam_pos[0],
                             s_houses[i].base[2] - s_cam_pos[2]);
    if (lod == LOD_FAR)
      continue;
    r_mesh(lod == LOD_NEAR ? &s_house : &s_house_lod, (const float *)world,
           white);
  }
}

/* The depot pad and sign. */
static void emit_depot(float t) {
  (void)t;
  r_pass(R_PASS_PROPS);
  const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};

  mat4 world;
  glm_mat4_identity(world);
  if (lod_level(s_depot[0] - s_cam_pos[0], s_depot[2] - s_cam_pos[2]) ==
      LOD_FAR)
    return;
  glm_translate(world, s_depot);
  r_mesh(&s_depot_mesh, (const float *)world, white);
}

/* The residents, each eased around to watch the player when nearby. */
static void emit_residents(float t) {
  (void)t;
  r_pass(R_PASS_PROPS);

  for (int i = 0; i < HOUSE_COUNT; i++) {
    mat4 world;
    glm_mat4_identity(world);
    glm_translate(world, s_houses[i].resident);
    glm_rotate(world, s_houses[i].look, (vec3){0.0f, 1.0f, 0.0f});
    if (lod_level(s_houses[i].resident[0] - s_cam_pos[0],
                  s_houses[i].resident[2] - s_cam_pos[2]) != LOD_NEAR)
      continue;
    r_mesh(&s_player_mesh, (const float *)world, HOUSE_TINT[i]);
  }
}

/*
 * Objective beacon: stacked camera-facing quads form a soft light shaft, with a
 * ground ring built as a closed ribbon. Cyan marks the depot, green the target
 * resident. No-op once delivered.
 */
static void emit_beacon(float t) {
  if (s_quest == GAME_QUEST_DONE)
    return;

  const float *p = active_target();
  const int pickup = (s_quest == GAME_QUEST_REQUEST);
  float color[4] = {pickup ? 0.35f : 0.45f, pickup ? 0.85f : 1.0f,
                    pickup ? 1.0f : 0.45f, 0.0f};

  r_pass(R_PASS_PARTICLES);
  for (int k = 0; k < 4; k++) {
    color[3] = 0.34f - (float)k * 0.07f;
    float bb[3] = {p[0], p[1] + 1.0f + (float)k * 1.6f + 0.1f * sinf(t * 2.0f),
                   p[2]};
    r_billboard(bb, 1.2f - (float)k * 0.18f, 1.8f, color);
  }

  enum { RING = 12 };
  float pts[(RING + 1) * 3];
  for (int i = 0; i <= RING; i++) {
    float a = (float)i / (float)RING * 6.2831853f;
    pts[i * 3 + 0] = p[0] + cosf(a) * 1.7f;
    pts[i * 3 + 1] = p[1] + 0.06f;
    pts[i * 3 + 2] = p[2] + sinf(a) * 1.7f;
  }
  color[3] = 0.55f;
  r_ribbon(pts, RING + 1, 0.22f, color);
}

/* The carry read on foot: a small pack on the player's back (island1-fhno). */
static void emit_pack(float t) {
  (void)t;
  if (!(s_quest == GAME_QUEST_CARRY && s_mode == MODE_FOOT))
    return;

  r_pass(R_PASS_VEHICLE);
  const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};

  mat4 world;
  glm_mat4_identity(world);
  glm_translate(world, s_player.pos);
  glm_rotate(world, s_player.heading, (vec3){0.0f, 1.0f, 0.0f});
  glm_translate(world, (vec3){0.0f, 1.05f, -0.22f});

  r_mesh(&s_pack, (const float *)world, white);
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
    if (s_mode == MODE_FOOT)
      chase_cam_update(&s_chase, s_player.pos, s_player.heading,
                       s_player.speed_kmh, s_view_w, s_view_h, frame_dt);
    else
      chase_cam_update(&s_chase, s_veh.pos, s_veh.heading, s_veh.speed_kmh,
                       s_view_w, s_view_h, frame_dt);
    r_camera((const float *)s_chase.view, (const float *)s_chase.proj);
  } else {
    cam_update(&s_cam, s_view_w, s_view_h);
    r_camera((const float *)s_cam.view, (const float *)s_cam.proj);
  }

  const float *cam = (s_cam_mode == CAM_CHASE) ? s_chase.pos : s_cam.pos;
  s_cam_pos[0] = cam[0];
  s_cam_pos[1] = cam[1];
  s_cam_pos[2] = cam[2];

  emit_sky();
  emit_ocean(s_time);
  terrain_emit(s_cam_pos);
  emit_wake(s_time);
  emit_depot(s_time);
  emit_houses(s_time);
  emit_residents(s_time);
  emit_car(s_time);
  if (s_mode == MODE_FOOT)
    emit_player(s_time);
  emit_pack(s_time);
  emit_parcel(s_time);
  emit_beacon(s_time);

  const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  char buf[48];

  if (s_hud) {
    int on_land = terrain_height_at(s_veh.pos[0], s_veh.pos[2]) > 0.0f;
    float pdx = s_player.pos[0] - s_veh.pos[0];
    float pdz = s_player.pos[2] - s_veh.pos[2];
    int near_car = pdx * pdx + pdz * pdz <= VEHICLE_REACH * VEHICLE_REACH;
    float speed_kmh =
        (s_mode == MODE_FOOT) ? s_player.speed_kmh : s_veh.speed_kmh;

    r_pass(R_PASS_HUD);
    snprintf(buf, sizeof(buf), "%d KMH", (int)(speed_kmh + 0.5f));
    r_text_2d(12, 10, 2.0f, s_mode == MODE_FOOT ? "ON FOOT" : "CHASE", white);
    r_text_2d(12, 32, 1.5f, buf, white);
    if (s_mode == MODE_FOOT)
      r_text_2d(12, 54, 1.0f, near_car ? "F GET IN" : "WASD WALK  F3 STATS",
                white);
    else
      r_text_2d(12, 54, 1.0f,
                on_land ? "F GET OUT  F3 STATS" : "DRIVE TO LAND TO DISEMBARK",
                white);

    /* Delivery objective (island1-pkcj/draq). */
    const float *ap = active_pos();
    const float *tp = active_target();
    float odx = ap[0] - tp[0];
    float odz = ap[2] - tp[2];
    float odist = sqrtf(odx * odx + odz * odz);

    if (s_quest == GAME_QUEST_REQUEST) {
      if (odist <= QUEST_PICKUP_RANGE)
        r_text_2d(12, 76, 1.0f, "E PICK UP PACKAGE", white);
      else {
        snprintf(buf, sizeof(buf), "DEPOT  %.0fm", odist);
        r_text_2d(12, 76, 1.0f, buf, white);
      }
    } else if (s_quest == GAME_QUEST_CARRY) {
      if (odist <= QUEST_DELIVER_RANGE)
        r_text_2d(12, 76, 1.0f, "E DELIVER TO RESIDENT", white);
      else {
        snprintf(buf, sizeof(buf), "HOUSE %d  %.0fm", s_target_house + 1,
                 odist);
        r_text_2d(12, 76, 1.0f, buf, white);
      }
    }
    snprintf(buf, sizeof(buf), "DELIVERED %d", s_delivered);
    r_text_2d(12, 94, 1.0f, buf, white);

    /* Center toast that fades after a delivery. */
    if (s_toast_timer > 0.0f) {
      float a = s_toast_timer / TOAST_TIME;
      if (a > 1.0f)
        a = 1.0f;
      const float toast_col[4] = {1.0f, 0.95f, 0.40f, a};
      const char *msg = "+1  PACKAGE DELIVERED";
      int tw = (int)(8.0f * 3.0f * (float)strlen(msg));
      r_text_2d((s_view_w - tw) / 2, s_view_h / 3, 3.0f, msg, toast_col);
    }
  }

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
    y += 12;

    int t_drawn = 0, t_total = 0;
    terrain_stats(&t_drawn, &t_total);
    snprintf(buf, sizeof(buf), "TERRAIN %d/%d", t_drawn, t_total);
    r_text_2d(x, y, 1.0f, buf, white);
  }

  /* Conversation textbox, last so it sits over every other HUD element
   * (island1-a2xf). It owns the bottom third; the beacon is world-space and
   * the readouts live above the box, so nothing is covered. */
  dialog_draw();
}
