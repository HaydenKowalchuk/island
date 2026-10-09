/* GL-free test for the front-end flow state machine (island1-tpz8): boot into
 * the intro, gameplay gated until PLAY, skip/confirm transitions and clean
 * pressed edges on the menu-nav actions. */
#include <assert.h>
#include <stdio.h>

#include "game.h"
#include "input.h"
#include "menu.h"

int main(void) {
    game_init();

    /* Boots to the intro. */
    assert(menu_screen() == MENU_SCREEN_INTRO);
    game_place_vehicle(0.0f, 0.0f, 0.0f);

    /* Any gameplay press skips the intro: the throttle press that starts
     * this test lands on the first tick and advances to MAIN. */
    input_event_key(ACT_THROTTLE, true);
    input_begin_frame();
    game_tick(1.0f / 60.0f);
    input_event_key(ACT_THROTTLE, false);
    input_begin_frame();
    game_tick(1.0f / 60.0f);
    assert(menu_screen() == MENU_SCREEN_MAIN);

    /* Gameplay is gated while the menu is up: a held throttle moves
     * nothing. */
    const float* parked = game_vehicle_pos();
    input_event_key(ACT_THROTTLE, true);
    for (int i = 0; i < 30; i++) {
        input_begin_frame();
        game_tick(1.0f / 60.0f);
    }
    input_event_key(ACT_THROTTLE, false);
    input_begin_frame();
    game_tick(1.0f / 60.0f);
    const float* after = game_vehicle_pos();
    assert(after[0] == parked[0] && after[2] == parked[2]);

    /* One confirm press skips the intro card (edges only, never held). */
    menu_set_screen(MENU_SCREEN_INTRO);
    input_event_key(ACT_MENU_CONFIRM, true);
    input_begin_frame();
    game_tick(1.0f / 60.0f);
    input_event_key(ACT_MENU_CONFIRM, false);
    input_begin_frame();
    game_tick(1.0f / 60.0f);
    assert(menu_screen() == MENU_SCREEN_MAIN);

    /* The intro also advances on its own timer with no input at all. */
    menu_set_screen(MENU_SCREEN_INTRO);
    for (int i = 0; i < 60 * 10; i++)
        menu_tick(1.0f / 60.0f);
    assert(menu_screen() == MENU_SCREEN_MAIN);

    /* Confirm on MAIN starts gameplay. */
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

    printf("menu: boot/gating/skip/confirm/stable + edges OK\n");
    game_shutdown();
    return 0;
}
