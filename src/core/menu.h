#ifndef ISLAND_CORE_MENU_H
#define ISLAND_CORE_MENU_H

/*
 * Front-end flow (island1-tpz8): intro -> main menu -> gameplay. This is the
 * app-flow state machine, distinct from GameMode (DRIVE/FOOT is player stance).
 *
 * The backend keeps calling game_tick()/game_render(); core branches on
 * menu_screen() internally, so no backend needs loop changes. menu_tick() runs
 * before the gameplay gates inside game_tick(); menu_render() records the
 * front-end overlay after the scene, inside game_render().
 */

typedef enum {
    MENU_SCREEN_INTRO,    /* title splash sequence (island1-zfn6) */
    MENU_SCREEN_MAIN,     /* main menu over the attract backdrop */
    MENU_SCREEN_CONTROLS, /* sub-screen: control bindings (island1-lb8c) */
    MENU_SCREEN_CREDITS,  /* sub-screen: credits (island1-lb8c) */
    MENU_SCREEN_PLAY      /* live gameplay */
} MenuScreen;

void menu_init(void);      /* boot state: starts on INTRO; called by game_init */
MenuScreen menu_screen(void);
void menu_tick(float dt);  /* input edges + intro timer + sub-screen nav */
void menu_render(void);    /* front-end overlay UI (island1-zfn6/lb8c) */

/* Test/dev reset: jump straight to a screen (gameplay tests enter PLAY). */
void menu_set_screen(MenuScreen screen);

#endif
