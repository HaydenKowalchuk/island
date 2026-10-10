/* GL-free test for the fog-unified LOD bands (island1-288p): the lod_level
 * boundaries, and props/particles/terrain culling together past FOG_END. */
#include "lod.h"

#include <assert.h>
#include <stdio.h>

#include "game.h"
#include "menu.h"
#include "render.h"

int main(void) {
  /* Band boundaries: NEAR <=80, MID <=140, FAR <= FOG_END (220), CULL past. */
  assert(lod_level(0.0f, 0.0f) == LOD_NEAR);
  assert(lod_level(50.0f, 0.0f) == LOD_NEAR);
  assert(lod_level(0.0f, -80.0f) == LOD_NEAR);
  assert(lod_level(80.5f, 0.0f) == LOD_MID);
  assert(lod_level(100.0f, 90.0f) == LOD_MID); /* ~134 */
  assert(lod_level(0.0f, -140.5f) == LOD_FAR);
  assert(lod_level(200.0f, 90.0f) == LOD_FAR); /* ~219 */
  assert(lod_level(0.0f, 230.0f) == LOD_CULL);
  assert(lod_level(600.0f, 600.0f) == LOD_CULL);

  /* Game level: near the depot the staged parcel, beacon and houses draw;
   * past FOG_END of every site nothing records — no floating props over
   * culled islands. */
  game_init();
  menu_set_screen(MENU_SCREEN_PLAY);

  const float* pkg = game_package_pos();
  game_place_vehicle(pkg[0], pkg[2] - 30.0f, 0.0f);
  for (int i = 0; i < 300; i++)
    game_render(1.0f / 60.0f); /* let the handover arc settle */
  const RenderFrame* f = r_frame();
  assert(f->pass_draws[R_PASS_PROPS] > 0);
  assert(f->pass_draws[R_PASS_PARTICLES] > 0);

  /* Open sea far from every house, depot and island. */
  game_place_vehicle(600.0f, 600.0f, 0.0f);
  for (int i = 0; i < 300; i++)
    game_render(1.0f / 60.0f);
  f = r_frame();
  assert(f->pass_draws[R_PASS_PROPS] == 0);
  assert(f->pass_draws[R_PASS_PARTICLES] == 0);
  assert(f->pass_draws[R_PASS_TERRAIN] == 0);

  printf("lod: bands + fog cull together OK\n");
  game_shutdown();
  return 0;
}
