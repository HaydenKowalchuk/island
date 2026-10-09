#include "menu.h"

#include "input.h"

/*
 * Front-end state machine. Gameplay rules live in game.c, which gates on
 * menu_screen(); this module only owns the flow and the intro timer. The
 * single-card timing is a placeholder until island1-zfn6 splits it into the
 * studio card + title splash with fades, and island1-lb8c adds the items.
 */
#define INTRO_TIME 4.0f

static MenuScreen s_screen;
static float s_intro_timer;

void menu_init(void) {
    s_screen = MENU_SCREEN_INTRO;
    s_intro_timer = INTRO_TIME;
}

MenuScreen menu_screen(void) {
    return s_screen;
}

void menu_set_screen(MenuScreen screen) {
    s_screen = screen;
    s_intro_timer = INTRO_TIME;
}

/* True on the tick that began this frame when any gameplay/menu action was
 * pressed. Dev actions (cam/debug/reset) do not skip the front-end. */
static int any_pressed(void) {
    const InputState *in = input_state();
    for (int i = 0; i < ACT_DEV_CAM; i++) {
        if (in->pressed[i])
            return 1;
    }
    return 0;
}

void menu_tick(float dt) {
    switch (s_screen) {
    case MENU_SCREEN_INTRO:
        /* One card per press; holding must not fast-forward (edges only). */
        s_intro_timer -= dt;
        if (s_intro_timer <= 0.0f || any_pressed())
            s_screen = MENU_SCREEN_MAIN;
        break;
    case MENU_SCREEN_MAIN:
        /* Item selection/nav arrive with island1-lb8c; confirm starts play. */
        if (input_pressed(ACT_MENU_CONFIRM))
            s_screen = MENU_SCREEN_PLAY;
        break;
    case MENU_SCREEN_CONTROLS:
    case MENU_SCREEN_CREDITS:
        if (input_pressed(ACT_MENU_BACK))
            s_screen = MENU_SCREEN_MAIN;
        break;
    case MENU_SCREEN_PLAY:
        /* No back-exit in v1: ESC still quits through the backend. */
        break;
    default:
        break;
    }
}

void menu_render(void) {
    /* Front-end UI arrives with island1-zfn6 (intro) and island1-lb8c (menu);
     * recorded over everything else at the end of game_render(). */
}
