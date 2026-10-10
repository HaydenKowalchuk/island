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

/* Current intro card (0-based); test/F3 use. */
int menu_intro_card(void);

/*
 * Attract backdrop (island1-7u5i): the menu camera drifts around `anchor` (a
 * point over water) while looking at `look` (on the island). game_init wires
 * both from the world layout.
 */
void menu_set_backdrop(float anchor_x, float anchor_z, float look_x,
                       float look_z);

/* Attract camera: rebuild view/proj for this frame's framebuffer size. */
void menu_cam_update(int width, int height);
const float *menu_cam_view(void);
const float *menu_cam_proj(void);
/* Attract camera eye (test/F3 use); valid after menu_cam_update. */
void menu_cam_eye(float out[3]);
/* Attract pose (eye/look/fov) for a camera handover (island1-38h3); the eye
 * is the last one menu_cam_update rendered. */
void menu_cam_pose(float pos[3], float look[3], float *fov);

#endif
