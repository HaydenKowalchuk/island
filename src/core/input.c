#include "input.h"

#include <string.h>

/* Events written by the backend, consumed by input_poll(). */
static uint8_t s_pending_down[ACT_COUNT];
static uint8_t s_pending_up[ACT_COUNT];
static float s_pending_mouse_dx;
static float s_pending_mouse_dy;

static InputState s_state;

void input_init(void) {
    memset(&s_state, 0, sizeof(s_state));
    memset(s_pending_down, 0, sizeof(s_pending_down));
    memset(s_pending_up, 0, sizeof(s_pending_up));
    s_pending_mouse_dx = 0.0f;
    s_pending_mouse_dy = 0.0f;
}

void input_event_key(Action action, bool down) {
    if (action < 0 || action >= ACT_COUNT)
        return;

    if (down)
        s_pending_down[action] = 1;
    else
        s_pending_up[action] = 1;
}

void input_event_mouse(float dx, float dy) {
    s_pending_mouse_dx += dx;
    s_pending_mouse_dy += dy;
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
