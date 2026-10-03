#include "input.h"

#include <string.h>

/*
 * Pending state is written by the backend (via input_event_*) and latched into
 * the public snapshot by input_begin_frame(). Edges are computed against the
 * previous latched level, so a key tapped between two ticks still produces one
 * press and one release.
 */
static uint8_t s_pending_held[ACT_COUNT];
static uint8_t s_pending_down[ACT_COUNT];
static uint8_t s_pending_up[ACT_COUNT];
static uint8_t s_last_held[ACT_COUNT];

static float s_pending_axis[AXIS_COUNT];
static float s_pending_mouse_x;
static float s_pending_mouse_y;

static InputState s_state;

void input_init(void) {
    memset(&s_state, 0, sizeof(s_state));
    memset(s_pending_held, 0, sizeof(s_pending_held));
    memset(s_pending_down, 0, sizeof(s_pending_down));
    memset(s_pending_up, 0, sizeof(s_pending_up));
    memset(s_last_held, 0, sizeof(s_last_held));
    memset(s_pending_axis, 0, sizeof(s_pending_axis));
    s_pending_mouse_x = 0.0f;
    s_pending_mouse_y = 0.0f;
}

void input_event_key(Action action, bool down) {
    if (action < 0 || action >= ACT_COUNT)
        return;

    s_pending_held[action] = down ? 1u : 0u;
    if (down)
        s_pending_down[action] = 1u;
    else
        s_pending_up[action] = 1u;
}

void input_event_axis(AxisId axis, float value) {
    if (axis < 0 || axis >= AXIS_COUNT)
        return;

    /* Axes are absolute levels; look is an accumulated per-tick delta. */
    if (axis == AX_LOOK_X || axis == AX_LOOK_Y)
        s_pending_axis[axis] += value;
    else
        s_pending_axis[axis] = value;
}

void input_event_mouse(float dx, float dy) {
    s_pending_mouse_x += dx;
    s_pending_mouse_y += dy;
}

void input_begin_frame(void) {
    for (int i = 0; i < ACT_COUNT; i++) {
        uint8_t prev = s_last_held[i];
        uint8_t now = s_pending_held[i];

        /* A tap that came and went between ticks still counts as a press. */
        if (s_pending_down[i] && !s_pending_up[i])
            now = 1u;

        s_state.held[i] = now;
        s_state.pressed[i] = (uint8_t)((s_pending_down[i] || (now && !prev)) ? 1u : 0u);
        s_state.released[i] = (uint8_t)((s_pending_up[i] || (!now && prev)) ? 1u : 0u);
        s_last_held[i] = now;

        s_pending_down[i] = 0u;
        s_pending_up[i] = 0u;
    }

    for (int a = 0; a < AXIS_COUNT; a++)
        s_state.axis[a] = s_pending_axis[a];
    s_state.axis[AX_LOOK_X] += s_pending_mouse_x;
    s_state.axis[AX_LOOK_Y] += s_pending_mouse_y;
    s_pending_mouse_x = 0.0f;
    s_pending_mouse_y = 0.0f;
    s_pending_axis[AX_LOOK_X] = 0.0f;
    s_pending_axis[AX_LOOK_Y] = 0.0f;
}

const InputState* input_state(void) {
    return &s_state;
}

bool input_held(Action a) {
    return a >= 0 && a < ACT_COUNT && s_state.held[a] != 0;
}

bool input_pressed(Action a) {
    return a >= 0 && a < ACT_COUNT && s_state.pressed[a] != 0;
}

bool input_released(Action a) {
    return a >= 0 && a < ACT_COUNT && s_state.released[a] != 0;
}

float input_axis(AxisId a) {
    return (a >= 0 && a < AXIS_COUNT) ? s_state.axis[a] : 0.0f;
}
