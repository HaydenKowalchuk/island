/* GL-free test for the MVP delivery loop (island1-pkcj). */
#include <assert.h>
#include <stdio.h>

#include "game.h"
#include "input.h"

static void tap_interact(void) {
    input_event_key(ACT_INTERACT, true);
    input_begin_frame();
    game_tick(1.0f / 60.0f);
    input_event_key(ACT_INTERACT, false);
    input_begin_frame();
    game_tick(1.0f / 60.0f);
}

int main(void) {
    game_init();

    assert(game_quest_phase() == GAME_QUEST_REQUEST);
    assert(!game_quest_carried());
    assert(game_delivered_count() == 0);

    /* Drive (teleport) onto the pickup islet and collect the package. */
    const float* pkg = game_package_pos();
    float px = pkg[0], pz = pkg[2];
    game_place_vehicle(px, pz, 0.0f);
    tap_interact();
    assert(game_quest_carried());
    assert(game_quest_phase() == GAME_QUEST_CARRY);

    /* Bring it back to the resident. */
    const float* npc = game_npc_pos();
    game_place_vehicle(npc[0], npc[2], 0.0f);
    tap_interact();
    assert(game_quest_phase() == GAME_QUEST_DONE);
    assert(game_delivered_count() == 1);

    /* After the short pause a fresh request spawns on a different islet. */
    int guard = 0;
    while (game_quest_phase() == GAME_QUEST_DONE && guard < 600) {
        input_begin_frame();
        game_tick(1.0f / 60.0f);
        guard++;
    }
    assert(game_quest_phase() == GAME_QUEST_REQUEST);
    assert(!game_quest_carried());

    const float* pkg2 = game_package_pos();
    float moved = (pkg2[0] - px) * (pkg2[0] - px) + (pkg2[2] - pz) * (pkg2[2] - pz);
    assert(moved > 1.0f);

    printf("quest: pickup -> deliver -> new request OK\n");
    game_shutdown();
    return 0;
}
