#ifndef ISLAND_CORE_INPUT_H
#define ISLAND_CORE_INPUT_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Platform-agnostic input contract. Backends translate their native events
 * (GLFW keys, PSP pad, VMU) into named actions and feed them here.
 */
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

/* Resets input state. Call once at startup. */
void input_init(void);

/* Backend event injection. */
void input_event_key(Action action, bool down);
void input_event_mouse(float dx, float dy);

/*
 * Begins a logic tick: clears the edge flags and the accumulated mouse delta.
 * Call once per fixed tick, before folding events.
 */
void input_begin_frame(void);

/* Folds injected events into the state. Call once per fixed tick. */
void input_poll(void);

const InputState* input_state(void);
bool input_held(Action a);
bool input_pressed(Action a);
bool input_released(Action a);

/* Axis helpers built on the raw action state. */
float input_axis_steer(void);    /* -1 left .. +1 right */
float input_axis_throttle(void); /* -1 brake .. +1 throttle */

#endif
