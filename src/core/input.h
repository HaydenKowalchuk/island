#ifndef ISLAND_CORE_INPUT_H
#define ISLAND_CORE_INPUT_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Platform-agnostic input contract. The game core sees only named actions and
 * analog axes; each backend (desktop GLFW now, PSP/Dreamcast later) maps its
 * own hardware onto them. No platform headers belong in this file.
 */

typedef enum {
    ACT_THROTTLE,
    ACT_BRAKE,
    ACT_BOOST,
    ACT_INTERACT, /* context: pickup / deliver / talk */
    ACT_VEHICLE,  /* enter / exit vehicle */
    ACT_LOOK,     /* held while looking (mouse drag; always-on on consoles) */
    ACT_PAUSE,
    ACT_QUIT,
    /* dev-only actions, outside the gameplay contract */
    ACT_DEV_CAM,
    ACT_DEV_DEBUG,
    ACT_DEV_RESET,
    ACT_COUNT
} Action;

typedef enum {
    AX_STEER,  /* -1 left .. +1 right (absolute, held) */
    AX_LOOK_X, /* radians accumulated this tick */
    AX_LOOK_Y,
    AXIS_COUNT
} AxisId;

typedef struct {
    uint8_t held[ACT_COUNT];
    uint8_t pressed[ACT_COUNT];  /* rising edge on the tick that began this frame */
    uint8_t released[ACT_COUNT]; /* falling edge */
    float axis[AXIS_COUNT];
} InputState;

/* Core side. */
void input_init(void);          /* reset state; call once at startup */
void input_begin_frame(void);   /* once per logic tick: latch pending, compute edges */
const InputState* input_state(void);
bool input_held(Action a);
bool input_pressed(Action a);
bool input_released(Action a);
float input_axis(AxisId a);

/* Injection API used by the platform backends. */
void input_event_key(Action action, bool down);
void input_event_axis(AxisId axis, float value); /* absolute, except AX_LOOK_* */
void input_event_mouse(float dx, float dy);      /* accumulates into AX_LOOK_X/Y */

/* Implemented by each backend (src/backend/<plat>/input_*.c). */
void backend_input_init(void);
void backend_input_poll(void); /* fill pending hardware state before the ticks */

#endif
