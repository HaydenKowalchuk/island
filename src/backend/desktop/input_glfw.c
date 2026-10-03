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
    { GLFW_KEY_W, ACT_THROTTLE },
    { GLFW_KEY_UP, ACT_THROTTLE },
    { GLFW_KEY_S, ACT_BRAKE },
    { GLFW_KEY_DOWN, ACT_BRAKE },
    { GLFW_KEY_LEFT_SHIFT, ACT_BOOST },
    { GLFW_KEY_RIGHT_SHIFT, ACT_BOOST },
    { GLFW_KEY_E, ACT_INTERACT },
    { GLFW_KEY_F, ACT_VEHICLE },
    { GLFW_KEY_R, ACT_DEV_RESET },
    { GLFW_KEY_C, ACT_DEV_CAM },
    { GLFW_KEY_F3, ACT_DEV_DEBUG },
    { GLFW_KEY_ESCAPE, ACT_QUIT },
};

/* A/D (and the arrows) drive the steer axis: left=-1, right=+1. */
static uint8_t s_steer_left;
static uint8_t s_steer_right;

static uint8_t s_look_held;
static float s_mouse_dx;
static float s_mouse_dy;
static double s_last_mouse_x;
static double s_last_mouse_y;

static Action action_from_key(int key) {
    for (size_t i = 0; i < sizeof(s_bindings) / sizeof(s_bindings[0]); i++) {
        if (s_bindings[i].key == key)
            return s_bindings[i].action;
    }
    return ACT_COUNT;
}

static void key_cb(GLFWwindow* win, int key, int scancode, int action, int mods) {
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

    Action a = action_from_key(key);
    if (a == ACT_COUNT)
        return;

    input_event_key(a, down);
}

static void mouse_button_cb(GLFWwindow* win, int button, int action, int mods) {
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

static void cursor_cb(GLFWwindow* win, double x, double y) {
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

    if (s_mouse_dx != 0.0f || s_mouse_dy != 0.0f) {
        input_event_mouse(s_mouse_dx, s_mouse_dy);
        s_mouse_dx = 0.0f;
        s_mouse_dy = 0.0f;
    }
}

void input_glfw_init(GLFWwindow* window) {
    backend_input_init();

    glfwGetCursorPos(window, &s_last_mouse_x, &s_last_mouse_y);
    glfwSetKeyCallback(window, key_cb);
    glfwSetCursorPosCallback(window, cursor_cb);
    glfwSetMouseButtonCallback(window, mouse_button_cb);
}
