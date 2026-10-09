#include "input_glfw.h"

#include "input.h" /* core input contract */

/*
 * Desktop binding table. This is where GLFW keys/mouse buttons are mapped onto
 * the platform-agnostic Action enum. The PSP and Dreamcast backends have their
 * own tables and call the same input_event_* functions.
 */

typedef struct {
  int key; /* GLFW_KEY_* */
  Action action;
} KeyBinding;

static const KeyBinding s_bindings[] = {
    {GLFW_KEY_W, ACT_THROTTLE},
    {GLFW_KEY_UP, ACT_THROTTLE},
    {GLFW_KEY_S, ACT_BRAKE},
    {GLFW_KEY_DOWN, ACT_BRAKE},
    {GLFW_KEY_LEFT_SHIFT, ACT_BOOST},
    {GLFW_KEY_RIGHT_SHIFT, ACT_BOOST},
    {GLFW_KEY_E, ACT_INTERACT},
    {GLFW_KEY_F, ACT_VEHICLE},
    {GLFW_KEY_TAB, ACT_INVENTORY},
    {GLFW_KEY_R, ACT_DEV_RESET},
    {GLFW_KEY_C, ACT_DEV_CAM},
    {GLFW_KEY_F3, ACT_DEV_DEBUG},
    {GLFW_KEY_ESCAPE, ACT_QUIT},
    /* Menu-nav aliases (island1-tpz8): gameplay keys double as menu input, so
     * the same bindings work while the front-end is up. A key may map to
     * several actions; key_cb fires every match. */
    {GLFW_KEY_UP, ACT_MENU_UP},
    {GLFW_KEY_W, ACT_MENU_UP},
    {GLFW_KEY_DOWN, ACT_MENU_DOWN},
    {GLFW_KEY_S, ACT_MENU_DOWN},
    {GLFW_KEY_ENTER, ACT_MENU_CONFIRM},
    {GLFW_KEY_KP_ENTER, ACT_MENU_CONFIRM},
    {GLFW_KEY_SPACE, ACT_MENU_CONFIRM},
    {GLFW_KEY_ESCAPE, ACT_MENU_BACK},
    {GLFW_KEY_BACKSPACE, ACT_MENU_BACK},
};

/* A/D (and the arrows) drive the steer axis: left=-1, right=+1. */
static uint8_t s_steer_left;
static uint8_t s_steer_right;

/*
 * W/S (and the up/down arrows) feed the analog throttle/brake axes as a full
 * 0/1 level, alongside the digital ACT_THROTTLE/ACT_BRAKE actions. The console
 * backends instead map analog triggers onto these same axes (island1-553a).
 */
static uint8_t s_throttle;
static uint8_t s_brake;

static uint8_t s_look_held;
static float s_mouse_dx;
static float s_mouse_dy;
static double s_last_mouse_x;
static double s_last_mouse_y;

static void key_cb(GLFWwindow *win, int key, int scancode, int action,
                   int mods) {
  (void)win;
  (void)scancode;
  (void)mods;
  if (action == GLFW_REPEAT)
    return;

  bool down = action == GLFW_PRESS;

  if (key == GLFW_KEY_A || key == GLFW_KEY_LEFT)
    s_steer_left = down ? 1u : 0u;
  else if (key == GLFW_KEY_D || key == GLFW_KEY_RIGHT)
    s_steer_right = down ? 1u : 0u;
  else if (key == GLFW_KEY_W || key == GLFW_KEY_UP)
    s_throttle = down ? 1u : 0u;
  else if (key == GLFW_KEY_S || key == GLFW_KEY_DOWN)
    s_brake = down ? 1u : 0u;

  /* Fire every action bound to this key (a key may appear twice: ESC is
   * both quit and menu-back); unused screens ignore the extras. */
  for (size_t i = 0; i < sizeof(s_bindings) / sizeof(s_bindings[0]); i++) {
    if (s_bindings[i].key == key)
      input_event_key(s_bindings[i].action, down);
  }
}

static void mouse_button_cb(GLFWwindow *win, int button, int action, int mods) {
  (void)mods;
  if (button != GLFW_MOUSE_BUTTON_RIGHT)
    return;

  bool down = action == GLFW_PRESS;
  s_look_held = down ? 1u : 0u;
  input_event_key(ACT_LOOK, down);

  if (down) {
    glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwGetCursorPos(win, &s_last_mouse_x, &s_last_mouse_y);
  } else {
    glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
  }
}

static void cursor_cb(GLFWwindow *win, double x, double y) {
  (void)win;

  if (s_look_held) {
    s_mouse_dx += (float)(x - s_last_mouse_x);
    s_mouse_dy += (float)(y - s_last_mouse_y);
  }

  s_last_mouse_x = x;
  s_last_mouse_y = y;
}

void backend_input_init(void) {
  s_steer_left = 0u;
  s_steer_right = 0u;
  s_throttle = 0u;
  s_brake = 0u;
  s_look_held = 0u;
  s_mouse_dx = 0.0f;
  s_mouse_dy = 0.0f;
  s_last_mouse_x = 0.0;
  s_last_mouse_y = 0.0;
}

void backend_input_poll(void) {
  float steer = 0.0f;
  if (s_steer_right)
    steer += 1.0f;
  if (s_steer_left)
    steer -= 1.0f;
  input_event_axis(AX_STEER, steer);

  input_event_axis(AX_THROTTLE, s_throttle ? 1.0f : 0.0f);
  input_event_axis(AX_BRAKE, s_brake ? 1.0f : 0.0f);

  if (s_mouse_dx != 0.0f || s_mouse_dy != 0.0f) {
    input_event_mouse(s_mouse_dx, s_mouse_dy);
    s_mouse_dx = 0.0f;
    s_mouse_dy = 0.0f;
  }
}

void input_glfw_init(GLFWwindow *window) {
  backend_input_init();

  glfwGetCursorPos(window, &s_last_mouse_x, &s_last_mouse_y);
  glfwSetKeyCallback(window, key_cb);
  glfwSetCursorPosCallback(window, cursor_cb);
  glfwSetMouseButtonCallback(window, mouse_button_cb);
}
