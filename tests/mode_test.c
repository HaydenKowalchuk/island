/* GL-free test for the player mode state machine (island1-zku4). */
#include <assert.h>
#include <stdio.h>

#include "game.h"
#include "input.h"

int main(void) {
    game_init();

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

    /* Out again, then walk forward for a second: the player moves on land
     * while the parked car stays put. */
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

    const float* veh = game_vehicle_pos();
    assert(veh[0] > -1.0f && veh[0] < 1.0f);
    assert(veh[2] > 70.0f && veh[2] < 86.0f);

    printf("mode: enter/exit + walking OK\n");
    game_shutdown();
    return 0;
}
