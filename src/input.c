#include "input.h"

#include <string.h>

#include <GLFW/glfw3.h>

/* Flags written by GLFW callbacks, consumed by input_poll(). */
static uint8_t s_pending_down[ACT_COUNT];
static uint8_t s_pending_up[ACT_COUNT];
static float s_pending_mouse_dx;
static float s_pending_mouse_dy;

static InputState s_state;
static GLFWwindow* s_window = NULL;
static double s_last_mouse_x = 0.0;
static double s_last_mouse_y = 0.0;

typedef struct {
    int key;      /* GLFW_KEY_* */
    Action action;
} KeyBinding;

static const KeyBinding s_bindings[] = {
    { GLFW_KEY_W, ACT_THROTTLE },
    { GLFW_KEY_UP, ACT_THROTTLE },
    { GLFW_KEY_S, ACT_BRAKE },
    { GLFW_KEY_DOWN, ACT_BRAKE },
    { GLFW_KEY_A, ACT_STEER_L },
    { GLFW_KEY_LEFT, ACT_STEER_L },
    { GLFW_KEY_D, ACT_STEER_R },
    { GLFW_KEY_RIGHT, ACT_STEER_R },
    { GLFW_KEY_LEFT_SHIFT, ACT_BOOST },
    { GLFW_KEY_RIGHT_SHIFT, ACT_BOOST },
    { GLFW_KEY_LEFT_CONTROL, ACT_SLOW },
    { GLFW_KEY_RIGHT_CONTROL, ACT_SLOW },
    { GLFW_KEY_E, ACT_CONTEXT },
    { GLFW_KEY_R, ACT_RESET },
    { GLFW_KEY_ESCAPE, ACT_QUIT },
};

static const Action s_mouse_bindings[] = {
    GLFW_MOUSE_BUTTON_RIGHT, /* ACT_LOOK */
};

static Action action_from_mouse(int button) {
    for (size_t i = 0; i < sizeof(s_mouse_bindings) / sizeof(s_mouse_bindings[0]); i++) {
        if ((int)s_mouse_bindings[i] == button)
            return ACT_LOOK;
    }
    return ACT_COUNT;
}

static Action action_from_key(int key) {
    for (size_t i = 0; i < sizeof(s_bindings) / sizeof(s_bindings[0]); i++) {
        if (s_bindings[i].key == key)
            return s_bindings[i].action;
    }
    return ACT_COUNT;
}

/* ---- GLFW callbacks: record intent, never touch game state ---- */

static void key_cb(GLFWwindow* win, int key, int scancode, int action, int mods) {
    (void)win;
    (void)scancode;
    (void)mods;
    if (action == GLFW_REPEAT)
        return;

    Action a = action_from_key(key);
    if (a == ACT_COUNT)
        return;

    if (action == GLFW_PRESS)
        s_pending_down[a] = 1;
    else
        s_pending_up[a] = 1;
}

static void mouse_button_cb(GLFWwindow* win, int button, int action, int mods) {
    (void)mods;

    Action a = action_from_mouse(button);
    if (a == ACT_COUNT)
        return;

    if (action == GLFW_PRESS) {
        s_pending_down[a] = 1;
        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        glfwGetCursorPos(win, &s_last_mouse_x, &s_last_mouse_y);
    } else {
        s_pending_up[a] = 1;
        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}

static void cursor_cb(GLFWwindow* win, double x, double y) {
    (void)win;

    if (s_state.held[ACT_LOOK]) {
        s_pending_mouse_dx += (float)(x - s_last_mouse_x);
        s_pending_mouse_dy += (float)(y - s_last_mouse_y);
    }

    s_last_mouse_x = x;
    s_last_mouse_y = y;
}

void input_init(void) {
    memset(&s_state, 0, sizeof(s_state));
    memset(s_pending_down, 0, sizeof(s_pending_down));
    memset(s_pending_up, 0, sizeof(s_pending_up));
    s_pending_mouse_dx = 0.0f;
    s_pending_mouse_dy = 0.0f;

    s_window = glfwGetCurrentContext();
    if (!s_window)
        return;

    glfwGetCursorPos(s_window, &s_last_mouse_x, &s_last_mouse_y);

    glfwSetKeyCallback(s_window, key_cb);
    glfwSetCursorPosCallback(s_window, cursor_cb);
    glfwSetMouseButtonCallback(s_window, mouse_button_cb);
}

void input_begin_frame(void) {
    memset(s_state.pressed, 0, sizeof(s_state.pressed));
    memset(s_state.released, 0, sizeof(s_state.released));
    s_state.mouse_dx = 0.0f;
    s_state.mouse_dy = 0.0f;
}

void input_poll(void) {
    for (int i = 0; i < ACT_COUNT; i++) {
        if (s_pending_down[i]) {
            s_state.held[i] = 1;
            s_state.pressed[i] = 1;
        }
        if (s_pending_up[i]) {
            s_state.held[i] = 0;
            s_state.released[i] = 1;
        }
        s_pending_down[i] = 0;
        s_pending_up[i] = 0;
    }

    s_state.mouse_dx = s_pending_mouse_dx;
    s_state.mouse_dy = s_pending_mouse_dy;
    s_pending_mouse_dx = 0.0f;
    s_pending_mouse_dy = 0.0f;
}

const InputState* input_state(void) {
    return &s_state;
}

bool input_held(Action a) {
    return s_state.held[a] != 0;
}

bool input_pressed(Action a) {
    return s_state.pressed[a] != 0;
}

bool input_released(Action a) {
    return s_state.released[a] != 0;
}

float input_axis_steer(void) {
    float v = 0.0f;
    if (input_held(ACT_STEER_R)) v += 1.0f;
    if (input_held(ACT_STEER_L)) v -= 1.0f;
    return v;
}

float input_axis_throttle(void) {
    float v = 0.0f;
    if (input_held(ACT_THROTTLE)) v += 1.0f;
    if (input_held(ACT_BRAKE)) v -= 1.0f;
    return v;
}