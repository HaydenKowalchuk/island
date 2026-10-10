/* GL-free test for the front-end flow (island1-tpz8) and the attract backdrop
 * (island1-7u5i): boot into the intro, gameplay gated until PLAY, skip and
 * confirm transitions, and the menu camera drift. Intro card timings are
 * hand-tuned and deliberately not pinned here. */
#include "menu.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "chase_cam.h"
#include "game.h"
#include "input.h"
#include "render.h"
#include "terrain.h"

static float dist3(const float* a, const float* b) {
  float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
  return sqrtf(dx * dx + dy * dy + dz * dz);
}

/* One full press+release edge of `a` with a tick either side, the way a
 * backend's callback delivers it. */
static void tap(Action a) {
  input_event_key(a, true);
  input_begin_frame();
  game_tick(1.0f / 60.0f);
  input_event_key(a, false);
  input_begin_frame();
  game_tick(1.0f / 60.0f);
}

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
  const float* parked = game_vehicle_pos();
  input_event_key(ACT_BRAKE, true);
  for (int i = 0; i < 30; i++) {
    input_begin_frame();
    game_tick(1.0f / 60.0f);
  }
  input_event_key(ACT_BRAKE, false);
  input_begin_frame();
  game_tick(1.0f / 60.0f);
  const float* after = game_vehicle_pos();
  assert(after[0] == parked[0] && after[2] == parked[2]);

  /* Camera handover (island1-38h3): run the attract backdrop like the real
   * loop, then confirm — the chase must LERP from the drifting eye, never
   * cut to the vehicle framing. */
  for (int i = 0; i < 30; i++) {
    input_begin_frame();
    game_tick(1.0f / 60.0f);
  }
  game_render(1.0f / 60.0f);
  float e1[3], carp[3];
  const float* ep = game_cam_pos();
  e1[0] = ep[0];
  e1[1] = ep[1];
  e1[2] = ep[2];
  const float* cp = game_vehicle_pos();
  carp[0] = cp[0];
  carp[1] = cp[1];
  carp[2] = cp[2];

  input_event_key(ACT_MENU_CONFIRM, true);
  input_begin_frame();
  game_tick(1.0f / 60.0f);
  input_event_key(ACT_MENU_CONFIRM, false);
  input_begin_frame();
  game_tick(1.0f / 60.0f);
  game_render(1.0f / 60.0f);
  float c1[3];
  const float* c1p = game_cam_pos();
  c1[0] = c1p[0];
  c1[1] = c1p[1];
  c1[2] = c1p[2];
  float d1 = dist3(c1, carp);
  assert(d1 > 12.0f); /* one frame in: still near the attract eye, no jump */

  for (int i = 0; i < 120; i++) {
    input_begin_frame();
    game_tick(1.0f / 60.0f);
    game_render(1.0f / 60.0f);
  }
  float c2[3];
  const float* c2p = game_cam_pos();
  c2[0] = c2p[0];
  c2[1] = c2p[1];
  c2[2] = c2p[2];
  float d2 = dist3(c2, carp);
  assert(d2 < 9.0f);      /* converged to the vehicle framing */
  assert(d1 - d2 > 5.0f); /* it travelled — the lerp happened */

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
  const float* drove = game_vehicle_pos();
  assert(drove[0] != ox || drove[2] != oz);

  /* The four menu-nav actions produce clean pressed edges. */
  const Action nav[4] = {ACT_MENU_UP, ACT_MENU_DOWN, ACT_MENU_CONFIRM, ACT_MENU_BACK};
  for (int i = 0; i < 4; i++) {
    input_event_key(nav[i], true);
    input_begin_frame();
    assert(input_pressed(nav[i]));
    input_event_key(nav[i], false);
    input_begin_frame();
    assert(!input_pressed(nav[i]));
  }

  /* Pause menu (island1-8nfp): Start/P opens it over frozen gameplay. */
  tap(ACT_PAUSE);
  assert(menu_screen() == MENU_SCREEN_PAUSE);
  assert(menu_pause_item() == 0); /* RESUME is the default */

  /* Frozen: a held throttle moves nothing, and the camera holds its pose. */
  const float* pp = game_vehicle_pos();
  float p0 = pp[0], p2 = pp[2];
  game_render(1.0f / 60.0f);
  float eye0[3];
  const float* cam_hold = game_cam_pos();
  eye0[0] = cam_hold[0];
  eye0[1] = cam_hold[1];
  eye0[2] = cam_hold[2];
  input_event_key(ACT_THROTTLE, true);
  for (int i = 0; i < 30; i++) {
    input_begin_frame();
    game_tick(1.0f / 60.0f);
    game_render(1.0f / 60.0f);
  }
  input_event_key(ACT_THROTTLE, false);
  input_begin_frame();
  game_tick(1.0f / 60.0f);
  game_render(1.0f / 60.0f);
  pp = game_vehicle_pos();
  assert(pp[0] == p0 && pp[2] == p2);
  cam_hold = game_cam_pos();
  assert(cam_hold[0] == eye0[0] && cam_hold[1] == eye0[1] && cam_hold[2] == eye0[2]);

  /* Down to INVENTORY, open it, BACK returns to the pause menu (selection
   * preserved). */
  tap(ACT_MENU_DOWN);
  assert(menu_screen() == MENU_SCREEN_PAUSE && menu_pause_item() == 1);
  tap(ACT_MENU_CONFIRM);
  assert(menu_screen() == MENU_SCREEN_INVENTORY);
  tap(ACT_MENU_BACK);
  assert(menu_screen() == MENU_SCREEN_PAUSE && menu_pause_item() == 1);

  /* Controls entered from the pause menu returns HERE, not to the title. */
  tap(ACT_MENU_DOWN);
  assert(menu_pause_item() == 2);
  tap(ACT_MENU_CONFIRM);
  assert(menu_screen() == MENU_SCREEN_CONTROLS);
  tap(ACT_MENU_BACK);
  assert(menu_screen() == MENU_SCREEN_PAUSE);

  /* Quit leaves to the title... */
  tap(ACT_MENU_DOWN);
  assert(menu_pause_item() == 3);
  tap(ACT_MENU_CONFIRM);
  assert(menu_screen() == MENU_SCREEN_MAIN);

  /* ...and the title starts play again as usual. */
  tap(ACT_MENU_CONFIRM);
  assert(menu_screen() == MENU_SCREEN_PLAY);

  /* RESUME (the default) returns to gameplay; the pause key toggles too. */
  tap(ACT_PAUSE);
  assert(menu_screen() == MENU_SCREEN_PAUSE && menu_pause_item() == 0);
  tap(ACT_MENU_CONFIRM);
  assert(menu_screen() == MENU_SCREEN_PLAY);
  tap(ACT_PAUSE);
  assert(menu_screen() == MENU_SCREEN_PAUSE);
  tap(ACT_MENU_BACK);
  assert(menu_screen() == MENU_SCREEN_PLAY);

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
    assert(eye[1] > 0.0f);                             /* above sea level for the whole orbit */
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
