/* GL-free test for the player mode state machine (island1-zku4) and the
 * presentation pose easing (island1-9gio). */
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "game.h"
#include "input.h"
#include "menu.h"
#include "terrain.h"

int main(void) {
    game_init();
    /* Gameplay tests jump straight into PLAY (the boot state is INTRO). */
    menu_set_screen(MENU_SCREEN_PLAY);

    /* Over water, F is ignored: there is nothing to stand on. */
    game_place_vehicle(0.0f, 0.0f, 0.0f);
    assert(game_mode() == GAME_MODE_DRIVE);
    input_event_key(ACT_VEHICLE, true);
    input_begin_frame();
    game_tick(1.0f / 60.0f);
    input_event_key(ACT_VEHICLE, false);
    input_begin_frame();
    game_tick(1.0f / 60.0f);
    assert(game_mode() == GAME_MODE_DRIVE);

    /* Parked on the main island, F gets the player out onto land. */
    game_place_vehicle(0.0f, 78.0f, 0.0f);
    input_event_key(ACT_VEHICLE, true);
    input_begin_frame();
    game_tick(1.0f / 60.0f);
    input_event_key(ACT_VEHICLE, false);
    input_begin_frame();
    game_tick(1.0f / 60.0f);
    assert(game_mode() == GAME_MODE_FOOT);
    assert(game_player_pos()[1] > 0.0f);

    /* Standing next to the car, F gets the player back in. */
    input_event_key(ACT_VEHICLE, true);
    input_begin_frame();
    game_tick(1.0f / 60.0f);
    input_event_key(ACT_VEHICLE, false);
    input_begin_frame();
    game_tick(1.0f / 60.0f);
    assert(game_mode() == GAME_MODE_DRIVE);

    /*
     * Presentation pose (island1-9gio): over open water the terrain tilt is
     * gated off, so bank is a pure steering response and pitch a pure accel
     * response -- both must ease in under input and settle back to zero.
     */
    game_place_vehicle(0.0f, 0.0f, 0.0f);
    input_event_axis(AX_STEER, 1.0f);
    input_event_key(ACT_THROTTLE, true);
    float peak_pitch = 0.0f;
    for (int i = 0; i < 60; i++) {
        input_begin_frame();
        game_tick(1.0f / 60.0f);
        float p = fabsf(game_vehicle_pitch());
        if (p > peak_pitch)
            peak_pitch = p;
    }
    assert(fabsf(game_vehicle_bank()) > 0.05f);
    assert(peak_pitch > 0.01f);
    input_event_axis(AX_STEER, 0.0f);
    input_event_key(ACT_THROTTLE, false);
    for (int i = 0; i < 240; i++) {
        input_begin_frame();
        game_tick(1.0f / 60.0f);
    }
    assert(fabsf(game_vehicle_bank()) < 0.02f);
    assert(fabsf(game_vehicle_pitch()) < 0.02f);

    /* Out again, then walk forward for a second: the player moves on land
     * while the parked car stays put. */
    game_place_vehicle(0.0f, 78.0f, 0.0f);
    input_event_key(ACT_VEHICLE, true);
    input_begin_frame();
    game_tick(1.0f / 60.0f);
    input_event_key(ACT_VEHICLE, false);
    input_begin_frame();
    game_tick(1.0f / 60.0f);
    assert(game_mode() == GAME_MODE_FOOT);

    float px = game_player_pos()[0];
    float pz = game_player_pos()[2];
    input_event_key(ACT_THROTTLE, true);
    for (int i = 0; i < 60; i++) {
        input_begin_frame();
        game_tick(1.0f / 60.0f);
    }
    input_event_key(ACT_THROTTLE, false);
    input_begin_frame();
    game_tick(1.0f / 60.0f);

    float nx = game_player_pos()[0];
    float nz = game_player_pos()[2];
    assert((nx - px) * (nx - px) + (nz - pz) * (nz - pz) > 0.01f);
    assert(game_player_pos()[1] > 0.0f);

    /*
     * Camera-relative movement (island1-7duk): place_player faces +z, so the
     * camera basis is forward +z / right +x. Steer alone must strafe along +x
     * with no forward drift, not turn-in-place like the old tank controls.
     */
    game_place_player(-2.0f, 78.0f);
    float sx = game_player_pos()[0];
    float sz = game_player_pos()[2];
    input_event_axis(AX_STEER, 1.0f);
    for (int i = 0; i < 30; i++) {
        input_begin_frame();
        game_tick(1.0f / 60.0f);
    }
    input_event_axis(AX_STEER, 0.0f);
    float sdx = game_player_pos()[0] - sx;
    float sdz = game_player_pos()[2] - sz;
    assert(sdx > 0.3f);
    assert(fabsf(sdz) < sdx * 0.25f);

    /*
     * Wading (island1-7duk): probe for a shallow-water strip (between sea
     * level and the deep cutoff) that stays wade-deep for the next 3m along
     * +z, stand at its start, and confirm the pace is capped at half and the
     * in_water hook stays set.
     */
    float wx = 0.0f, wz = 0.0f;
    int found = 0;
    for (float z = 30.0f; z < 110.0f && !found; z += 0.5f) {
        for (float x = -50.0f; x < 50.0f && !found; x += 0.5f) {
            int ok = 1;
            for (int s = 0; s <= 6 && ok; s++) {
                float g = terrain_height_at(x, z + (float)s * 0.5f);
                if (g >= -0.1f || g <= -0.7f)
                    ok = 0;
            }
            if (ok) {
                wx = x;
                wz = z;
                found = 1;
            }
        }
    }
    assert(found);
    game_place_player(wx, wz);
    assert(game_player_in_water());
    input_event_key(ACT_THROTTLE, true);
    for (int i = 0; i < 40; i++) {
        input_begin_frame();
        game_tick(1.0f / 60.0f);
    }
    input_event_key(ACT_THROTTLE, false);
    /* Half pace: 40 ticks at 2.25 m/s ~ 1.5m; full pace would be ~3m. */
    float wdx = game_player_pos()[0] - wx;
    float wdz = game_player_pos()[2] - wz;
    float wdist = sqrtf(wdx * wdx + wdz * wdz);
    assert(game_player_in_water());
    assert(game_player_pos()[1] <= 0.01f); /* wading rides sea level */
    assert(wdist > 0.5f && wdist < 2.4f);

    /*
     * House collision (island1-7duk): walk straight into house 0 from 3m
     * south (place faces +z) and never end up inside the wall circle.
     */
    game_place_player(-14.0f, 67.0f);
    float min_d2 = 1.0e9f;
    input_event_key(ACT_THROTTLE, true);
    for (int i = 0; i < 180; i++) {
        input_begin_frame();
        game_tick(1.0f / 60.0f);
        float dx = game_player_pos()[0] - (-14.0f);
        float dz = game_player_pos()[2] - 70.0f;
        float d2 = dx * dx + dz * dz;
        if (d2 < min_d2)
            min_d2 = d2;
    }
    input_event_key(ACT_THROTTLE, false);
    assert(min_d2 > 1.7f * 1.7f); /* wall 2.2 + body 0.4, with slack */

    const float* veh = game_vehicle_pos();
    assert(veh[0] > -1.0f && veh[0] < 1.0f);
    assert(veh[2] > 70.0f && veh[2] < 86.0f);

    printf("mode: enter/exit + walking OK\n");
    game_shutdown();
    return 0;
}
