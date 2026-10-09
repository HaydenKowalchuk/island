#include "menu.h"

#include <math.h>
#include <string.h>

#include <cglm/cglm.h>

#include "chase_cam.h"
#include "input.h"
#include "render.h"

/*
 * Front-end state machine (island1-tpz8). Gameplay rules live in game.c, which
 * gates on menu_screen(); this module owns the flow, the intro cards
 * (island1-zfn6) and the attract backdrop (island1-7u5i).
 */

/* Intro cards: studio card, then the title splash. Any press skips one card. */
#define CARD_FADE_IN 0.5f
#define CARD_HOLD 1.4f
#define CARD_FADE_OUT 0.5f
#define CARD_TIME (CARD_FADE_IN + CARD_HOLD + CARD_FADE_OUT)
#define INTRO_CARDS 2

/*
 * Attract camera: a slow drift around a point over water, looking at the
 * island. The eye never leaves the sea, so no terrain clamp is needed; the
 * small orbit radius keeps the car (parked near the anchor) in frame for the
 * whole revolution.
 */
#define MENU_ORBIT_R 8.0f
#define MENU_ORBIT_RATE 0.08f /* rad/s, ~79s per revolution */
#define MENU_EYE_H 4.0f
#define MENU_EYE_BOB 0.4f
#define MENU_LOOK_H 12.0f
#define MENU_FOV 55.0f
#define MENU_ZNEAR 0.1f
#define MENU_ZFAR 1000.0f

static MenuScreen s_screen;
static int s_card;         /* intro card index */
static float s_card_timer; /* seconds into the current card */
static float s_menu_time;  /* attract-camera clock, advanced in menu_tick */

static float s_anchor[2]; /* orbit centre, over water */
static float s_look[2];   /* look-at point on the island */

static float s_eye[3];
static mat4 s_view;
static mat4 s_proj;

void menu_init(void) {
    s_screen = MENU_SCREEN_INTRO;
    s_card = 0;
    s_card_timer = 0.0f;
    s_menu_time = 0.0f;
}

MenuScreen menu_screen(void) {
    return s_screen;
}

int menu_intro_card(void) {
    return s_card;
}

void menu_set_screen(MenuScreen screen) {
    s_screen = screen;
    s_card = 0;
    s_card_timer = 0.0f;
}

void menu_set_backdrop(float anchor_x, float anchor_z, float look_x,
                       float look_z) {
    s_anchor[0] = anchor_x;
    s_anchor[1] = anchor_z;
    s_look[0] = look_x;
    s_look[1] = look_z;
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

/* Advance to the next intro card, or leave the intro when the last is done. */
static void intro_next(void) {
    s_card_timer = 0.0f;
    if (++s_card >= INTRO_CARDS)
        s_screen = MENU_SCREEN_MAIN;
}

void menu_tick(float dt) {
    s_menu_time += dt;

    switch (s_screen) {
    case MENU_SCREEN_INTRO:
        s_card_timer += dt;
        if (s_card_timer >= CARD_TIME)
            intro_next();
        else if (any_pressed())
            intro_next(); /* one card per press; edges only, never held */
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

/* Eased fade: smoothstep in during the fade-in, hold, out during fade-out. */
static float card_alpha(float progress) {
    float t_in = CARD_FADE_IN / CARD_TIME;
    float t_out = (CARD_FADE_IN + CARD_HOLD) / CARD_TIME;
    if (progress < t_in) {
        float x = progress / t_in;
        return x * x * (3.0f - 2.0f * x);
    }
    if (progress < t_out)
        return 1.0f;
    float x = (progress - t_out) / (1.0f - t_out);
    return 1.0f - x * x * (3.0f - 2.0f * x);
}

static void render_card(const char *title, const char *sub, float scale,
                        float sub_scale, float progress) {
    const RenderFrame *fr = r_frame();
    int w = fr->width;
    int h = fr->height;

    float a = card_alpha(progress);

    /* Fade veil over the whole screen. Over the black boot clear it is
     * invisible and the text carries the fade; if the scene is ever recorded
     * behind the card, the veil crossfades it in and out. */
    const float veil[4] = {0.0f, 0.0f, 0.0f, 1.0f - a};
    r_rect_2d(0.0f, 0.0f, 1.0f, 1.0f, veil);

    /* Text follows the fade; a lone title is vertically centred. */
    const float col[4] = {1.0f, 1.0f, 1.0f, a};
    int tw = (int)(8.0f * scale * (float)strlen(title));
    if (sub) {
        r_text_2d((w - tw) / 2, h / 2 - 48, scale, title, col);
        int sw = (int)(8.0f * sub_scale * (float)strlen(sub));
        r_text_2d((w - sw) / 2, h / 2 + 24, sub_scale, sub, col);
    } else {
        r_text_2d((w - tw) / 2, h / 2 - (int)(4.0f * scale), scale, title, col);
    }
}

void menu_render(void) {
    r_pass(R_PASS_HUD);

    if (s_screen == MENU_SCREEN_INTRO) {
        static const struct {
            const char *title;
            const char *sub;
            float scale;
            float sub_scale;
        } cards[INTRO_CARDS] = {
            {"AN AI GAMES PRODUCTION", NULL, 1.5f, 0.0f},
            {"ISLAND", "A PACKAGE DELIVERY GAME", 6.0f, 1.5f},
        };
        if (s_card < 0 || s_card >= INTRO_CARDS)
            return;
        render_card(cards[s_card].title, cards[s_card].sub, cards[s_card].scale,
                    cards[s_card].sub_scale, s_card_timer / CARD_TIME);
        return;
    }

    /* Menu UI arrives with island1-lb8c. */
}

void menu_cam_update(int width, int height) {
    float ang = s_menu_time * MENU_ORBIT_RATE;
    s_eye[0] = s_anchor[0] + sinf(ang) * MENU_ORBIT_R;
    s_eye[1] = MENU_EYE_H + sinf(s_menu_time * 0.5f) * MENU_EYE_BOB;
    s_eye[2] = s_anchor[1] + cosf(ang) * MENU_ORBIT_R;

    float look[3] = {s_look[0], MENU_LOOK_H, s_look[1]};
    glm_lookat(s_eye, look, (vec3){0.0f, 1.0f, 0.0f}, s_view);

    float aspect = (height > 0) ? (float)width / (float)height
                                : (16.0f / 9.0f);
    glm_perspective(chase_vfov_for_aspect(MENU_FOV, aspect), aspect,
                    MENU_ZNEAR, MENU_ZFAR, s_proj);
}

const float *menu_cam_view(void) {
    return (const float *)s_view;
}

const float *menu_cam_proj(void) {
    return (const float *)s_proj;
}

void menu_cam_eye(float out[3]) {
    out[0] = s_eye[0];
    out[1] = s_eye[1];
    out[2] = s_eye[2];
}
