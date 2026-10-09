#ifndef ISLAND_CORE_GAME_H
#define ISLAND_CORE_GAME_H

#include <stdint.h>

/*
 * Core game entry points. A backend owns the window/loop and drives the game:
 *
 *   game_init();
 *   game_resize(w, h);                     // on framebuffer resize
 *   per fixed tick: game_tick(TICK);       // after backend_input_poll()/input_begin_frame()
 *   per frame:      game_render(frame_dt); // records the frame via the RenderAPI
 *
 * game_render() records primitives through the RenderAPI only. The backend then
 * presents whatever r_frame() holds. `frame_dt` is the real (clamped) frame
 * delta, used by the chase camera spring which integrates per render frame.
 */
void game_init(void);
void game_shutdown(void);
void game_resize(int width, int height);
void game_tick(float dt);
void game_render(float frame_dt);

/* Player/vehicle mode (island1-zku4); also read by tests and debug tooling. */
typedef enum { GAME_MODE_DRIVE, GAME_MODE_FOOT } GameMode;
GameMode game_mode(void);
const float* game_player_pos(void);
const float* game_vehicle_pos(void);

/* Standing in the shallows or deeper (island1-7duk wading hook). */
int game_player_in_water(void);

/* Presentation pose, radians (island1-9gio): bank >0 leans the top left,
 * pitch >0 is nose-up. Drive mode only; both read zero on foot. */
float game_vehicle_bank(void);
float game_vehicle_pitch(void);

/* Teleport the parked car (spawn/reset/tests). Snaps it to the ground. */
void game_place_vehicle(float x, float z, float heading);

/* Teleport the walking player and enter foot mode (spawn/reset/tests). */
void game_place_player(float x, float z);

/* Gameplay HUD visibility (island1-fovq); the F3 debug overlay is separate. */
void game_set_hud(int on);
int game_hud_enabled(void);

/* Delivery mission state (island1-pkcj/draq); read by the HUD and tests. */
typedef enum {
  GAME_QUEST_REQUEST, /* package is waiting at the depot */
  GAME_QUEST_CARRY,   /* picked up, deliver to the target resident */
  GAME_QUEST_DONE     /* delivered; a new request spawns after a short pause */
} GameQuestPhase;
GameQuestPhase game_quest_phase(void);
int game_quest_carried(void);
int game_delivered_count(void);

/* Target resident (the current recipient house's resident). */
const float *game_npc_pos(void);
const float *game_resident_pos(int house); /* any house, clamped */
const float *game_package_pos(void);
const float *game_depot_pos(void);
int game_target_house(void);

/* Objective beacon: where it is and whether it is currently shown. */
int game_beacon_active(void);
const float *game_beacon_pos(void);

/* Parcel payload (island1-iim2). An ADDRESSED parcel has a readable label:
 * it belongs to the resident and is delivered sealed — the player never
 * opens it. A LOST parcel (obscured label / partially open) is ours: claiming
 * moves its stacks into the player inventory. */
typedef enum { GAME_PARCEL_ADDRESSED, GAME_PARCEL_LOST } GameParcelKind;
#define GAME_PARCEL_MAX_ITEMS 3
GameParcelKind game_parcel_kind(void);
int game_parcel_slot_count(void);
/* Stack i of the current payload; returns 0 past the end. */
int game_parcel_slot(int i, uint16_t *id, uint16_t *count);
/* (Re)load the payload — quest spawn / tests. Empty or zero stacks drop out. */
void game_parcel_configure(GameParcelKind kind, const uint16_t *ids,
                           const uint16_t *counts, int n);
/* LOST only: move contents into the player inventory. Returns 1 when
 * anything was claimed (leftovers stay in the parcel if the bag is full). */
int game_parcel_claim(void);

#endif
