#include "input_dc.h"

#include <math.h>

#include <kos.h>

#include "input.h" /* core input contract */

/*
 * Dreamcast binding table (island1-19s3's provisional mapping). This is where
 * maple controller hardware is mapped onto the platform-agnostic Action/axis
 * contract via the same input_event_* functions the desktop backend calls.
 *
 *   A (green)      -> throttle (and AX_THROTTLE)  B (blue) -> brake (AX_BRAKE)
 *   L trigger      -> boost (analog fine: past half pull)
 *   Y (red)        -> interact (pickup / deliver / talk)
 *   X (yellow)     -> vehicle (enter / exit)
 *   analog stick   -> AX_STEER (x) and fwd/back (y -> AX_THROTTLE/AX_BRAKE), so
 *                     the car and the on-foot player both steer and move on
 *                     one stick; D-pad steers when the stick is centred
 *   Start          -> pause
 *   A+B+X+Y+Start  -> quit (KOS convention for exiting)
 *
 * No mouse: ACT_LOOK stays 0, chase cam only. Dev keys (F3 stats, fly cam)
 * have no binding; add one when needed.
 */
#define NUB_RANGE 127.0f
/* The stick is sensitive; remaining travel is remapped to 0..1 so full
 * deflection still reaches a full-value axis. */
#define STEER_DEADZONE 0.30f
#define FWD_DEADZONE 0.30f

/* Digital button edges are emitted only on change, like GLFW's key callback. */
static unsigned int s_prev_buttons;
static int s_prev_boost;
static int s_prev_quit;

static void key_event(unsigned int buttons, unsigned int mask, Action action) {
    int now = (buttons & mask) ? 1 : 0;
    int was = (s_prev_buttons & mask) ? 1 : 0;
    if (now != was)
        input_event_key(action, now);
}

/* Signed stick axis (-1..1) with a deadzone; remaining travel remapped. */
static float stick_axis(signed char raw, float deadzone) {
    float v = (float)raw / NUB_RANGE;
    float a = fabsf(v);
    if (a < deadzone)
        return 0.0f;
    float scaled = (a - deadzone) / (1.0f - deadzone);
    return (v < 0.0f) ? -scaled : scaled;
}

void input_dc_init(void) {
    backend_input_init();
}

void backend_input_init(void) {
    s_prev_buttons = 0;
    s_prev_boost = 0;
    s_prev_quit = 0;
}

void backend_input_poll(void) {
    maple_device_t* cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if (!cont)
        return;
    cont_state_t* st = (cont_state_t*)maple_dev_status(cont);
    if (!st)
        return;

    unsigned int buttons = st->buttons;

    key_event(buttons, CONT_A, ACT_THROTTLE);
    key_event(buttons, CONT_B, ACT_BRAKE);
    key_event(buttons, CONT_Y, ACT_INTERACT);
    key_event(buttons, CONT_X, ACT_VEHICLE);
    key_event(buttons, CONT_START, ACT_PAUSE);

    int boost_now = st->ltrig > 128;
    if (boost_now != s_prev_boost)
        input_event_key(ACT_BOOST, boost_now);
    s_prev_boost = boost_now;

    /* Analog steer with a deadzone; D-pad overrides when the stick is
     * centred. joyx is a signed byte, -128..127. */
    float steer = stick_axis(st->joyx, STEER_DEADZONE);
    if (steer == 0.0f) {
        if (buttons & CONT_DPAD_RIGHT)
            steer = 1.0f;
        else if (buttons & CONT_DPAD_LEFT)
            steer = -1.0f;
    }
    input_event_axis(AX_STEER, steer);

    /* Stick up/down drives throttle/brake (up = positive). Combine with the
     * digital A/B so the stick alone moves the car or the walking player. */
    float fwd = -stick_axis(st->joyy, FWD_DEADZONE);
    float throttle = (buttons & CONT_A) ? 1.0f : 0.0f;
    float brake = (buttons & CONT_B) ? 1.0f : 0.0f;
    if (fwd > throttle)
        throttle = fwd;
    if (-fwd > brake)
        brake = -fwd;
    input_event_axis(AX_THROTTLE, throttle);
    input_event_axis(AX_BRAKE, brake);

    /* Exit combo: all face buttons + Start. */
    unsigned int combo = CONT_A | CONT_B | CONT_X | CONT_Y | CONT_START;
    int quit_now = (buttons & combo) == combo;
    if (quit_now != s_prev_quit)
        input_event_key(ACT_QUIT, quit_now);
    s_prev_quit = quit_now;

    s_prev_buttons = buttons;
}
