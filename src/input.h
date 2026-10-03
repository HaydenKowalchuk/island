#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ACT_THROTTLE,
    ACT_BRAKE,
    ACT_STEER_L,
    ACT_STEER_R,
    ACT_BOOST,
    ACT_SLOW,
    ACT_CONTEXT,   /* pickup / deliver / talk */
    ACT_LOOK,
    ACT_RESET,
    ACT_QUIT,
    ACT_COUNT
} Action;

typedef struct {
    uint8_t held[ACT_COUNT];
    uint8_t pressed[ACT_COUNT];  /* rising edge on the tick that began this frame */
    uint8_t released[ACT_COUNT]; /* falling edge */
    float mouse_dx;
    float mouse_dy;
} InputState;

/* Registers the GLFW callbacks that feed the input manager. */
void input_init(void);

/*
 * Begins a logic tick: clears the edge flags and the accumulated mouse delta.
 * Call once per fixed tick, before polling.
 */
void input_begin_frame(void);

/*
 * Folds pending GLFW events into the state. Call once per fixed tick, after
 * glfwPollEvents().
 */
void input_poll(void);

const InputState* input_state(void);
bool input_held(Action a);
bool input_pressed(Action a);
bool input_released(Action a);

/* Axis helpers built on the raw action state. */
float input_axis_steer(void);  /* -1 left .. +1 right */
float input_axis_throttle(void); /* -1 brake .. +1 throttle */

#endif