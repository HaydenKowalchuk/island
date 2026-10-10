/* GL-free test for the delivery mission loop (island1-pkcj/draq), the parcel
 * payload model (island1-iim2: sealed turn-in vs lost-parcel claim) and the
 * story quest chain (island1-f9x2: talk -> pickup -> deliver -> grant ->
 * claim -> talk -> chain reward). */
#include "quest.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "dialog.h"
#include "game.h"
#include "input.h"
#include "inventory.h"
#include "items.h"
#include "menu.h"

static void tap_interact(void) {
  input_event_key(ACT_INTERACT, true);
  input_begin_frame();
  game_tick(1.0f / 60.0f);
  input_event_key(ACT_INTERACT, false);
  input_begin_frame();
  game_tick(1.0f / 60.0f);
}

/* Advance the conversation textbox with E until it closes (island1-a2xf). */
static void dismiss_dialog(void) {
  int guard = 0;
  while (dialog_active() && guard++ < 32)
    tap_interact();
  assert(!dialog_active());
}

/* Stand at a resident and open a conversation (one tap). */
static void visit_and_talk(int house) {
  const float* r = game_resident_pos(house);
  game_place_player(r[0], r[2]);
  tap_interact();
  assert(dialog_active());
  dismiss_dialog();
}

int main(void) {
  game_init();
  /* Gameplay tests jump straight into PLAY (the boot state is INTRO). */
  menu_set_screen(MENU_SCREEN_PLAY);

  assert(game_quest_phase() == GAME_QUEST_REQUEST);
  assert(!game_quest_carried());
  assert(game_delivered_count() == 0);
  assert(game_beacon_active());
  assert(inv_used() == 0); /* game_init starts with an empty bag */

  /* The package waits at the depot and the beacon marks it. */
  const float* depot = game_depot_pos();
  const float* pkg = game_package_pos();
  assert(pkg[0] == depot[0] && pkg[2] == depot[2]);
  const float* beacon = game_beacon_pos();
  assert(beacon[0] == depot[0] && beacon[2] == depot[2]);

  int first_house = game_target_house();

  /* Collect the package at the depot (opens the depot keeper's box). */
  /* The request spawns a sealed parcel addressed to the target house. */
  assert(game_parcel_kind() == GAME_PARCEL_ADDRESSED);
  assert(game_parcel_slot_count() == 1);
  uint16_t pid = 0, pq = 0;
  assert(game_parcel_slot(0, &pid, &pq));
  assert(pid != 0 && pq == 1);
  assert(!game_parcel_claim()); /* sealed parcels are never ours to open */

  game_place_vehicle(depot[0], depot[2], 0.0f);
  tap_interact();
  assert(game_quest_carried());
  assert(game_quest_phase() == GAME_QUEST_CARRY);
  assert(dialog_active());

  /* The box holds the delivery E until dismissed, like a player would. */
  assert(dialog_active());

  /* Render while the box is open — the exact path that crashed strlen via
   * a dangling s_lines (island1-a2xf). Must record the textbox, not fault. */
  game_render(1.0f / 60.0f);
  assert(game_quest_carried());

  dismiss_dialog();

  /* The beacon now marks the target resident. */
  const float* npc = game_npc_pos();
  beacon = game_beacon_pos();
  assert(beacon[0] == npc[0] && beacon[2] == npc[2]);

  /* Deliver by talking to the resident on foot (island1-9fm3): park at the
   * door, get out, and the conversation completes the hand-off once its
   * reaction line is dismissed. */
  game_place_vehicle(npc[0], npc[2], 0.0f);
  input_event_key(ACT_VEHICLE, true);
  input_begin_frame();
  game_tick(1.0f / 60.0f);
  input_event_key(ACT_VEHICLE, false);
  input_begin_frame();
  game_tick(1.0f / 60.0f);
  assert(game_mode() == GAME_MODE_FOOT);

  tap_interact();
  assert(dialog_active()); /* the resident's reaction line is up */
  assert(game_quest_phase() == GAME_QUEST_CARRY);
  dismiss_dialog();
  assert(game_quest_phase() == GAME_QUEST_DONE);
  assert(game_delivered_count() == 1);
  assert(!game_beacon_active());
  /* Sealed: contents went over whole, the bag never saw them. */
  assert(game_parcel_slot_count() == 0);
  assert(inv_count((int)pid) == 0);

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

  /* Lost parcels (obscured label) are ours: claim moves stacks into the
   * inventory; addressed ones still refuse (island1-iim2). */
  {
    uint16_t ids[2] = {ITEM_FISH, ITEM_SEASHELL};
    uint16_t qty[2] = {3, 1};
    game_parcel_configure(GAME_PARCEL_LOST, ids, qty, 2);
    assert(game_parcel_kind() == GAME_PARCEL_LOST);
    assert(game_parcel_claim() == 1);
    assert(inv_count(ITEM_FISH) == 3);
    assert(inv_count(ITEM_SEASHELL) == 1);
    assert(game_parcel_slot_count() == 0);

    game_parcel_configure(GAME_PARCEL_ADDRESSED, ids, qty, 2);
    assert(game_parcel_claim() == 0);
    assert(inv_count(ITEM_FISH) == 3); /* untouched */
    assert(game_parcel_slot_count() == 2);
  }

  /* ---- Story chain (island1-f9x2): talk -> pickup -> deliver -> grant
   * -> claim -> talk -> chain reward. ---- */
  quest_start_chain(1);
  assert(strcmp(quest_chain_name(), "THE LIGHTHOUSE KEY") == 0);
  assert(quest_step_type() == QSTEP_TALK);
  assert(quest_step_target() == 0);
  assert(quest_step_prompt() != 0);

  visit_and_talk(0); /* ask Marlowe about the lighthouse */
  assert(quest_step_type() == QSTEP_PICKUP);
  assert(!quest_carried());
  /* The key parcel is staged at the depot by the step-entry hook. */
  assert(game_parcel_kind() == GAME_PARCEL_ADDRESSED);
  assert(game_parcel_slot_count() == 1);
  {
    uint16_t id = 0, q = 0;
    assert(game_parcel_slot(0, &id, &q));
    assert(id == ITEM_BRASS_KEY && q == 1);
  }

  depot = game_depot_pos();
  game_place_player(depot[0], depot[2]);
  tap_interact(); /* BREN hands over the key parcel */
  assert(quest_carried());
  dismiss_dialog();
  assert(quest_step_type() == QSTEP_DELIVER);
  assert(quest_step_target() == 3);

  visit_and_talk(3); /* sealed hand-off to PIP */
  assert(!quest_carried());
  assert(game_delivered_count() == 2);
  assert(quest_step_type() == QSTEP_GRANT);

  /* The GRANT step auto-plays on the next tick and queues its toast. */
  input_begin_frame();
  game_tick(1.0f / 60.0f);
  assert(quest_step_type() == QSTEP_CLAIM);
  assert(inv_count(ITEM_SEASHELL) == 2); /* one was kept in the iim2 block */
  {
    const char* gift = quest_take_toast();
    assert(gift != 0 && strstr(gift, "SEASHELL") != 0);
  }

  /* Next tick stages the lost parcel at the depot. */
  input_begin_frame();
  game_tick(1.0f / 60.0f);
  assert(game_parcel_kind() == GAME_PARCEL_LOST);
  assert(game_parcel_slot_count() == 1);

  game_place_player(depot[0], depot[2]);
  tap_interact();                    /* claim the wreck's fish */
  assert(inv_count(ITEM_FISH) == 5); /* 3 kept earlier + 2 in the parcel */
  assert(game_parcel_slot_count() == 0);
  assert(quest_step_type() == QSTEP_TALK);
  assert(quest_step_target() == 1);

  visit_and_talk(1); /* tell Tamsin — the chain completes */
  assert(quest_paused());
  assert(game_quest_phase() == GAME_QUEST_DONE);
  assert(inv_count(ITEM_CHART) == 1); /* chain reward */
  {
    const char* gift = quest_take_toast();
    assert(gift != 0 && strstr(gift, "ISLAND CHART") != 0);
  }

  /* After the breather the delivery run rotates back in. */
  guard = 0;
  while (game_quest_phase() == GAME_QUEST_DONE && guard < 600) {
    input_begin_frame();
    game_tick(1.0f / 60.0f);
    guard++;
  }
  assert(game_quest_phase() == GAME_QUEST_REQUEST);
  assert(strcmp(quest_chain_name(), "ISLAND DELIVERY RUN") == 0);
  assert(game_target_house() != 3); /* never straight back to PIP */

  printf("quest: pickup -> deliver -> new recipient OK\n");
  printf("quest: story chain talk/pickup/deliver/grant/claim/reward OK\n");
  game_shutdown();
  return 0;
}
