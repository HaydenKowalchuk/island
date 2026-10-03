/* GL-free test for the delivery mission loop (island1-pkcj/draq). */
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
    assert(game_beacon_active());

    /* The package waits at the depot and the beacon marks it. */
    const float* depot = game_depot_pos();
    const float* pkg = game_package_pos();
    assert(pkg[0] == depot[0] && pkg[2] == depot[2]);
    const float* beacon = game_beacon_pos();
    assert(beacon[0] == depot[0] && beacon[2] == depot[2]);

    int first_house = game_target_house();

    /* Collect the package at the depot. */
    game_place_vehicle(depot[0], depot[2], 0.0f);
    tap_interact();
    assert(game_quest_carried());
    assert(game_quest_phase() == GAME_QUEST_CARRY);

    /* The beacon now marks the target resident. */
    const float* npc = game_npc_pos();
    beacon = game_beacon_pos();
    assert(beacon[0] == npc[0] && beacon[2] == npc[2]);

    /* Deliver to the resident. */
    game_place_vehicle(npc[0], npc[2], 0.0f);
    tap_interact();
    assert(game_quest_phase() == GAME_QUEST_DONE);
    assert(game_delivered_count() == 1);
    assert(!game_beacon_active());

    /* After the pause a fresh request picks a different recipient. */
    int guard = 0;
    while (game_quest_phase() == GAME_QUEST_DONE && guard < 600) {
        input_begin_frame();
        game_tick(1.0f / 60.0f);
        guard++;
    }
    assert(game_quest_phase() == GAME_QUEST_REQUEST);
    assert(game_target_house() != first_house);
    assert(game_beacon_active());
    depot = game_depot_pos();
    beacon = game_beacon_pos();
    assert(beacon[0] == depot[0] && beacon[2] == depot[2]);

    printf("quest: pickup -> deliver -> new recipient OK\n");
    game_shutdown();
    return 0;
}
