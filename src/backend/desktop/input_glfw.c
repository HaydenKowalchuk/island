#include "input_glfw.h"

#include "input.h" /* core input contract */

/*
 * Desktop binding table. This is where GLFW keys/mouse buttons are mapped onto
 * the platform-agnostic Action enum. The PSP and Dreamcast backends will have
 * their own tables and call the same input_event_* functions.
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
    { GLFW_KEY_F, ACT_TOGGLE_CAM },
    { GLFW_KEY_ESCAPE, ACT_QUIT },
};

static double s_last_mouse_x = 0.0;
static double s_last_mouse_y = 0.0;

static Action action_from_key(int key) {
    for (size_t i = 0; i < sizeof(s_bindings) / sizeof(s_bindings[0]); i++) {
        if (s_bindings[i].key == key)
            return s_bindings[i].action;
    }
    return ACT_COUNT;
}

static Action action_from_mouse(int button) {
    if (button == GLFW_MOUSE_BUTTON_RIGHT)
        return ACT_LOOK;
    return ACT_COUNT;
}

static void key_cb(GLFWwindow* win, int key, int scancode, int action, int mods) {
    (void)win;
    (void)scancode;
    (void)mods;
    if (action == GLFW_REPEAT)
        return;

    Action a = action_from_key(key);
    if (a == ACT_COUNT)
        return;

    input_event_key(a, action == GLFW_PRESS);
}

static void mouse_button_cb(GLFWwindow* win, int button, int action, int mods) {
    (void)mods;

    Action a = action_from_mouse(button);
    if (a == ACT_COUNT)
        return;

    if (action == GLFW_PRESS) {
        input_event_key(a, true);
        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        glfwGetCursorPos(win, &s_last_mouse_x, &s_last_mouse_y);
    } else {
        input_event_key(a, false);
        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}

static void cursor_cb(GLFWwindow* win, double x, double y) {
    (void)win;

    if (input_held(ACT_LOOK))
        input_event_mouse((float)(x - s_last_mouse_x), (float)(y - s_last_mouse_y));

    s_last_mouse_x = x;
    s_last_mouse_y = y;
}

void input_glfw_init(GLFWwindow* window) {
    glfwGetCursorPos(window, &s_last_mouse_x, &s_last_mouse_y);

    glfwSetKeyCallback(window, key_cb);
    glfwSetCursorPosCallback(window, cursor_cb);
    glfwSetMouseButtonCallback(window, mouse_button_cb);
}
