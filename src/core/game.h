#ifndef ISLAND_CORE_GAME_H
#define ISLAND_CORE_GAME_H

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

/* Teleport the parked car (spawn/reset/tests). Snaps it to the ground. */
void game_place_vehicle(float x, float z, float heading);

/* Delivery quest state (island1-pkcj); read by the HUD and tests. */
typedef enum {
  GAME_QUEST_REQUEST, /* package is waiting on an islet */
  GAME_QUEST_CARRY,   /* picked up, return to the resident */
  GAME_QUEST_DONE     /* delivered; a new request spawns after a short pause */
} GameQuestPhase;
GameQuestPhase game_quest_phase(void);
int game_quest_carried(void);
int game_delivered_count(void);
const float *game_npc_pos(void);
const float *game_package_pos(void);

#endif
