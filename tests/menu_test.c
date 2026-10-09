/* GL-free test for the front-end flow (island1-tpz8) and the attract backdrop
 * (island1-7u5i): boot into the intro, gameplay gated until PLAY, skip and
 * confirm transitions, and the menu camera drift. Intro card timings are
 * hand-tuned and deliberately not pinned here. */
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "chase_cam.h"
#include "game.h"
#include "input.h"
#include "menu.h"
#include "render.h"
#include "terrain.h"

int main(void) {
  game_init();

  /* Boots to the intro. */
  assert(menu_screen() == MENU_SCREEN_INTRO);
  game_place_vehicle(0.0f, 0.0f, 0.0f);

  /* Any gameplay press skips a card; keep pressing until the menu. */
  int guard = 0;
  while (menu_screen() != MENU_SCREEN_MAIN && guard++ < 8) {
    input_event_key(ACT_THROTTLE, true);
    input_begin_frame();
    game_tick(1.0f / 60.0f);
    input_event_key(ACT_THROTTLE, false);
    input_begin_frame();
    game_tick(1.0f / 60.0f);
  }
  assert(menu_screen() == MENU_SCREEN_MAIN);

  /* Gameplay is gated while the menu is up: a held driving input moves
   * nothing. The throttle would start play, so use the brake. */
  const float *parked = game_vehicle_pos();
  input_event_key(ACT_BRAKE, true);
  for (int i = 0; i < 30; i++) {
    input_begin_frame();
    game_tick(1.0f / 60.0f);
  }
  input_event_key(ACT_BRAKE, false);
  input_begin_frame();
  game_tick(1.0f / 60.0f);
  const float *after = game_vehicle_pos();
  assert(after[0] == parked[0] && after[2] == parked[2]);

  /* The intro also advances on its own timer with no input at all; the
   * exact card timings are hand-tuned and not pinned here. */
  menu_set_screen(MENU_SCREEN_INTRO);
  for (int i = 0; i < 60 * 30; i++)
    menu_tick(1.0f / 60.0f);
  assert(menu_screen() == MENU_SCREEN_MAIN);

  /* Confirm starts gameplay from the menu. */
  input_event_key(ACT_MENU_CONFIRM, true);
  input_begin_frame();
  game_tick(1.0f / 60.0f);
  input_event_key(ACT_MENU_CONFIRM, false);
  input_begin_frame();
  game_tick(1.0f / 60.0f);
  assert(menu_screen() == MENU_SCREEN_PLAY);

  /* PLAY is stable: BACK does not exit (no pause menu in v1). */
  input_event_key(ACT_MENU_BACK, true);
  input_begin_frame();
  game_tick(1.0f / 60.0f);
  input_event_key(ACT_MENU_BACK, false);
  input_begin_frame();
  game_tick(1.0f / 60.0f);
  assert(menu_screen() == MENU_SCREEN_PLAY);

  /* Gameplay resumes: the car drives again. */
  float ox = after[0], oz = after[2];
  input_event_key(ACT_THROTTLE, true);
  for (int i = 0; i < 30; i++) {
    input_begin_frame();
    game_tick(1.0f / 60.0f);
  }
  input_event_key(ACT_THROTTLE, false);
  input_begin_frame();
  game_tick(1.0f / 60.0f);
  const float *drove = game_vehicle_pos();
  assert(drove[0] != ox || drove[2] != oz);

  /* The four menu-nav actions produce clean pressed edges. */
  const Action nav[4] = {ACT_MENU_UP, ACT_MENU_DOWN, ACT_MENU_CONFIRM,
                         ACT_MENU_BACK};
  for (int i = 0; i < 4; i++) {
    input_event_key(nav[i], true);
    input_begin_frame();
    assert(input_pressed(nav[i]));
    input_event_key(nav[i], false);
    input_begin_frame();
    assert(!input_pressed(nav[i]));
  }

  /* Attract camera (island1-7u5i): the drift stays above the water, keeps
   * the island within fog range, and moves smoothly. */
  menu_set_screen(MENU_SCREEN_MAIN);
  menu_set_backdrop(0.0f, -24.0f, 0.0f, 78.0f);
  menu_cam_update(1280, 720);
  float prev[3];
  menu_cam_eye(prev);
  for (int i = 0; i < 60 * 90; i++) { /* a full revolution of the drift */
    menu_tick(1.0f / 60.0f);
    menu_cam_update(1280, 720);
    float eye[3];
    menu_cam_eye(eye);
    assert(eye[1] > 0.0f); /* above sea level for the whole orbit */
    assert(terrain_height_at(eye[0], eye[2]) <= 0.0f); /* over water */
    float dx = eye[0] - prev[0];
    float dy = eye[1] - prev[1];
    float dz = eye[2] - prev[2];
    float step = sqrtf(dx * dx + dy * dy + dz * dz);
    assert(step > 0.0f && step < 0.05f); /* smooth, no jitter */
    prev[0] = eye[0];
    prev[1] = eye[1];
    prev[2] = eye[2];
  }
  /* The island stays within fog range of the eye (no pop-in). */
  float ix = prev[0];
  float iz = prev[2] - 78.0f;
  assert(sqrtf(ix * ix + iz * iz) < FOG_END);

  /* The 16:9-referenced aspect rule (island1-t21d) holds for the menu cam:
   * 16:9 and 4:3 see the same horizontal width. */
  float vf169 = chase_vfov_for_aspect(55.0f, 16.0f / 9.0f);
  float vf43 = chase_vfov_for_aspect(55.0f, 4.0f / 3.0f);
  float h169 = 2.0f * atanf(tanf(vf169 * 0.5f) * (16.0f / 9.0f));
  float h43 = 2.0f * atanf(tanf(vf43 * 0.5f) * (4.0f / 3.0f));
  assert(fabsf(h169 - h43) < 1.0e-5f);

  printf("menu: boot/gating/skip/confirm/stable + attract cam OK\n");
  game_shutdown();
  return 0;
}
