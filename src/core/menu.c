#include "menu.h"

#include <cglm/cglm.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "chase_cam.h"
#include "input.h"
#include "inventory.h"
#include "items.h"
#include "render.h"

/*
 * Front-end state machine (island1-tpz8). Gameplay rules live in game.c, which
 * gates on menu_screen(); this module owns the flow, the intro cards
 * (island1-zfn6), the attract backdrop (island1-7u5i) and the pause menu
 * (island1-8nfp).
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

/* Pause menu (island1-8nfp): the items, in draw/selection order. */
static const char* const PAUSE_ITEMS[] = {"RESUME", "INVENTORY", "CONTROLS", "QUIT TO TITLE"};
#define PAUSE_COUNT ((int)(sizeof(PAUSE_ITEMS) / sizeof(PAUSE_ITEMS[0])))

static MenuScreen s_screen;
static int s_card;            /* intro card index */
static float s_card_timer;    /* seconds into the current card */
static float s_menu_time;     /* attract-camera clock, advanced in menu_tick */
static int s_pause_sel;       /* selected pause-menu item */
static MenuScreen s_sub_from; /* where CONTROLS/CREDITS returns to */

static float s_anchor[2]; /* orbit centre, over water */
static float s_look[2];   /* look-at point on the island */

static float s_eye[3];
static mat4 s_view;
static mat4 s_proj;

void menu_init(void) {
  s_screen = MENU_SCREEN_INTRO;
  // s_screen = MENU_SCREEN_PLAY;
  s_card = 0;
  s_card_timer = 0.0f;
  s_menu_time = 0.0f;
  s_pause_sel = 0;
  s_sub_from = MENU_SCREEN_MAIN;
}

MenuScreen menu_screen(void) {
  return s_screen;
}

int menu_intro_card(void) {
  return s_card;
}

int menu_pause_item(void) {
  return s_pause_sel;
}

void menu_set_screen(MenuScreen screen) {
  s_screen = screen;
  s_card = 0;
  s_card_timer = 0.0f;
  s_pause_sel = 0;
}

void menu_set_backdrop(float anchor_x, float anchor_z, float look_x, float look_z) {
  s_anchor[0] = anchor_x;
  s_anchor[1] = anchor_z;
  s_look[0] = look_x;
  s_look[1] = look_z;
}

/* True on the tick that began this frame when any gameplay/menu action was
 * pressed. Dev actions (cam/debug/reset) do not skip the front-end. */
static int any_pressed(void) {
  const InputState* in = input_state();
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
      /* Confirm or the throttle starts play — the attract hint invites the
       * throttle. Item selection/nav arrive with island1-lb8c. */
      if (input_pressed(ACT_MENU_CONFIRM) || input_pressed(ACT_THROTTLE))
        s_screen = MENU_SCREEN_PLAY;
      break;
    case MENU_SCREEN_CONTROLS:
    case MENU_SCREEN_CREDITS:
      if (input_pressed(ACT_MENU_BACK))
        s_screen = s_sub_from;
      break;
    case MENU_SCREEN_PLAY:
      /* Start (consoles) / P (desktop) opens the pause menu (island1-8nfp).
       * No back-exit in v1: ESC still quits through the backend. */
      if (input_pressed(ACT_PAUSE)) {
        s_pause_sel = 0;
        s_screen = MENU_SCREEN_PAUSE;
      }
      break;
    case MENU_SCREEN_PAUSE:
      /* The pause key or BACK resumes; everything else is item navigation.
       * game.c freezes the sim and the camera while this screen is up. */
      if (input_pressed(ACT_PAUSE) || input_pressed(ACT_MENU_BACK)) {
        s_screen = MENU_SCREEN_PLAY;
        break;
      }
      if (input_pressed(ACT_MENU_UP))
        s_pause_sel = (s_pause_sel + PAUSE_COUNT - 1) % PAUSE_COUNT;
      if (input_pressed(ACT_MENU_DOWN))
        s_pause_sel = (s_pause_sel + 1) % PAUSE_COUNT;
      if (input_pressed(ACT_MENU_CONFIRM)) {
        switch (s_pause_sel) {
          case 0:
            s_screen = MENU_SCREEN_PLAY;
            break;
          case 1:
            s_screen = MENU_SCREEN_INVENTORY;
            break;
          case 2:
            s_sub_from = MENU_SCREEN_PAUSE;
            s_screen = MENU_SCREEN_CONTROLS;
            break;
          default:
            /* Quit to title: back to MAIN over the attract backdrop. The
             * world keeps its state for now; a fresh-run reset is separate. */
            s_screen = MENU_SCREEN_MAIN;
            break;
        }
      }
      break;
    case MENU_SCREEN_INVENTORY:
      if (input_pressed(ACT_MENU_BACK) || input_pressed(ACT_PAUSE))
        s_screen = MENU_SCREEN_PAUSE;
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

static void render_card(const char* title, const char* sub, float scale, float sub_scale, float progress) {
  const RenderFrame* fr = r_frame();
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
      const char* title;
      const char* sub;
      float scale;
      float sub_scale;
    } cards[INTRO_CARDS] = {
        {"AN AI GAMES PRODUCTION", NULL, 1.5f, 0.0f},
        {"ISLAND", "A PACKAGE DELIVERY GAME", 6.0f, 1.5f},
    };
    if (s_card < 0 || s_card >= INTRO_CARDS)
      return;
    render_card(cards[s_card].title, cards[s_card].sub, cards[s_card].scale, cards[s_card].sub_scale, s_card_timer / CARD_TIME);
    return;
  }

  /* Attract hint (island1-7u5i): pulsing invite above the bottom edge;
   * the proper menu items arrive with island1-lb8c. */
  if (s_screen == MENU_SCREEN_MAIN) {
    const RenderFrame* fr = r_frame();
    float a = 0.55f + 0.25f * sinf(s_menu_time * 2.5f);
    const float dim[4] = {1.0f, 1.0f, 1.0f, a};
    static const char hint[] = "PRESS THROTTLE TO START";
    int hw = (int)(8.0f * 1.5f * (float)(sizeof(hint) - 1));
    r_text_2d((fr->width - hw) / 2, fr->height - 56, 1.5f, hint, dim);
    return;
  }

  /* Pause menu (island1-8nfp): veil over the frozen scene, one centred
   * column, the selected item marked with "> " and picked out in colour. */
  if (s_screen == MENU_SCREEN_PAUSE) {
    const RenderFrame* fr = r_frame();
    const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    const float dim[4] = {1.0f, 1.0f, 1.0f, 0.55f};
    const float sel[4] = {1.0f, 0.92f, 0.45f, 1.0f};
    const float veil[4] = {0.0f, 0.0f, 0.0f, 0.55f};
    r_rect_2d(0.0f, 0.0f, 1.0f, 1.0f, veil);

    static const char title[] = "PAUSED";
    int tw = (int)(8.0f * 2.0f * (float)(sizeof(title) - 1));
    r_text_2d((fr->width - tw) / 2, fr->height / 2 - 72, 2.0f, title, white);

    char row[32];
    for (int i = 0; i < PAUSE_COUNT; i++) {
      snprintf(row, sizeof(row), "%s%s", i == s_pause_sel ? "> " : "  ", PAUSE_ITEMS[i]);
      int rw = (int)(8.0f * 1.5f * (float)strlen(row));
      r_text_2d((fr->width - rw) / 2, fr->height / 2 - 24 + i * 18, 1.5f, row, i == s_pause_sel ? sel : dim);
    }
    return;
  }

  if (s_screen == MENU_SCREEN_INVENTORY) {
    menu_inventory_panel("BACK TO PAUSE");
    return;
  }

  /* Controls card. island1-lb8c owns the real sub-screens; this is the
   * stop-gap so the pause menu's Controls entry is not a blank screen. The
   * rows cover all three desktop and console bindings in one list. */
  if (s_screen == MENU_SCREEN_CONTROLS) {
    const RenderFrame* fr = r_frame();
    const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    const float veil[4] = {0.0f, 0.0f, 0.0f, 0.65f};
    r_rect_2d(0.0f, 0.0f, 1.0f, 1.0f, veil);
    static const char title[] = "CONTROLS";
    int tw = (int)(8.0f * 2.0f * (float)(sizeof(title) - 1));
    r_text_2d((fr->width - tw) / 2, fr->height / 2 - 76, 2.0f, title, white);
    static const char* const rows[] = {
        "DRIVE      W/S OR L/R TRIGGERS",
        "WALK       WASD OR STICK",
        "BOOST      SHIFT  CROSS/A",
        "INTERACT   E  SQUARE/Y",
        "VEHICLE    F  TRIANGLE/X",
        "INVENTORY  TAB/SELECT",
        "PAUSE      P/START",
        "BACK       ESC/B/CIRCLE",
    };
    for (int i = 0; i < (int)(sizeof(rows) / sizeof(rows[0])); i++)
      r_text_2d(fr->width / 2 - 120, fr->height / 2 - 48 + i * 12, 1.0f, rows[i], white);
    return;
  }
}

void menu_inventory_panel(const char* close_hint) {
  r_pass(R_PASS_HUD);

  const RenderFrame* fr = r_frame();
  const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  int rows = inv_used();
  if (rows < 1)
    rows = 1;
  int bw = 208;
  int bh = 40 + rows * 10;
  int bx = (fr->width - bw) / 2;
  int by = (fr->height - bh) / 2;
  const float panel[4] = {0.01f, 0.02f, 0.05f, 0.88f};
  const float edge[4] = {0.72f, 0.82f, 0.95f, 0.95f};
  /* px -> normalized, y flipped bottom-up like dialog.c's rect_px. */
  float nx = (float)bx / (float)fr->width;
  float ny = 1.0f - (float)(by + bh) / (float)fr->height;
  r_rect_2d(nx - 0.004f, ny - 0.010f, (float)bw / (float)fr->width + 0.008f, (float)bh / (float)fr->height + 0.020f, edge);
  r_rect_2d(nx, ny, (float)bw / (float)fr->width, (float)bh / (float)fr->height, panel);
  r_text_2d(bx + 8, by + 6, 1.0f, "INVENTORY", white);
  if (inv_used() == 0) {
    r_text_2d(bx + 8, by + 20, 1.0f, "EMPTY", white);
  } else {
    char buf[48];
    for (int i = 0; i < inv_used(); i++) {
      const InvSlot* slot = inv_slot(i);
      const ItemDef* def = item_def(slot->id);
      snprintf(buf, sizeof(buf), "%s  x%d", def ? def->name : "?", slot->count);
      r_text_2d(bx + 8, by + 20 + i * 10, 1.0f, buf, white);
    }
  }
  r_text_2d(bx + 8, by + bh - 12, 1.0f, close_hint, white);
}

int menu_consumes_back(void) {
  switch (s_screen) {
    case MENU_SCREEN_PAUSE:
    case MENU_SCREEN_INVENTORY:
    case MENU_SCREEN_CONTROLS:
    case MENU_SCREEN_CREDITS:
      return 1;
    default:
      return 0;
  }
}

int menu_attract_backdrop(void) {
  switch (s_screen) {
    case MENU_SCREEN_MAIN:
      return 1;
    case MENU_SCREEN_CONTROLS:
    case MENU_SCREEN_CREDITS:
      /* Controls entered from the pause menu keeps the frozen scene. */
      return s_sub_from == MENU_SCREEN_MAIN;
    default:
      return 0;
  }
}

void menu_cam_update(int width, int height) {
  float ang = s_menu_time * MENU_ORBIT_RATE;
  s_eye[0] = s_anchor[0] + sinf(ang) * MENU_ORBIT_R;
  s_eye[1] = MENU_EYE_H + sinf(s_menu_time * 0.5f) * MENU_EYE_BOB;
  s_eye[2] = s_anchor[1] + cosf(ang) * MENU_ORBIT_R;

  float look[3] = {s_look[0], MENU_LOOK_H, s_look[1]};
  glm_lookat(s_eye, look, (vec3){0.0f, 1.0f, 0.0f}, s_view);

  float aspect = (height > 0) ? (float)width / (float)height : (16.0f / 9.0f);
  glm_perspective(chase_vfov_for_aspect(MENU_FOV, aspect), aspect, MENU_ZNEAR, MENU_ZFAR, s_proj);
}

const float* menu_cam_view(void) {
  return (const float*)s_view;
}

const float* menu_cam_proj(void) {
  return (const float*)s_proj;
}

void menu_cam_eye(float out[3]) {
  out[0] = s_eye[0];
  out[1] = s_eye[1];
  out[2] = s_eye[2];
}

void menu_cam_pose(float pos[3], float look[3], float* fov) {
  pos[0] = s_eye[0];
  pos[1] = s_eye[1];
  pos[2] = s_eye[2];
  look[0] = s_look[0];
  look[1] = MENU_LOOK_H;
  look[2] = s_look[1];
  *fov = MENU_FOV;
}
