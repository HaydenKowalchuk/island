#include <math.h>

#include <pspctrl.h>

#include "game.h"  /* game_mode(): the mapping is mode-aware */
#include "input.h" /* core input contract */

/*
 * PSP controller binding table (island1-rggc, mode-aware). The pad is digital
 * plus a small analog nub; there is no mouse, so ACT_LOOK is never held and
 * the chase camera stays automatic.
 *
 *   L        -> throttle (vehicle, digital)   R  -> brake (vehicle, digital)
 *   Cross    -> boost                             Square -> ACT_INTERACT
 *   Triangle -> ACT_VEHICLE                       Start  -> ACT_PAUSE
 *   Select   -> ACT_DEV_DEBUG                     Home   -> ACT_QUIT
 *   nub X    -> AX_STEER; nub Y -> fwd/back on foot only (AX_THROTTLE /
 *              AX_BRAKE), so the walking player moves on the stick alone;
 *              D-pad steers when the nub is centred.
 *
 * The vehicle is driven ONLY by the triggers; on foot the stick alone moves.
 */

#define NUB_CENTER 128
#define NUB_RANGE 127
/* Large deadzone (the nub is very sensitive); the remaining travel is remapped
 * to 0..1 so full deflection still reaches a full-value axis. */
#define STEER_DEADZONE 0.30f
#define FWD_DEADZONE 0.30f

/* Digital button edges are emitted only on change, like GLFW's key callback. */
static unsigned int s_prev_buttons;

static void key_event(unsigned int buttons, unsigned int mask, Action action) {
    int now = (buttons & mask) ? 1 : 0;
    int was = (s_prev_buttons & mask) ? 1 : 0;
    if (now != was)
        input_event_key(action, now);
}

static float nub_axis(unsigned char raw, float deadzone) {
    float v = ((float)raw - (float)NUB_CENTER) / (float)NUB_RANGE;
    if (v < -1.0f)
        v = -1.0f;
    if (v > 1.0f)
        v = 1.0f;
    float a = fabsf(v);
    if (a < deadzone)
        return 0.0f;
    float scaled = (a - deadzone) / (1.0f - deadzone);
    return (v < 0.0f) ? -scaled : scaled;
}

void backend_input_init(void) {
    s_prev_buttons = 0;
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
}

void backend_input_poll(void) {
    SceCtrlData pad;
    if (!sceCtrlPeekBufferPositive(&pad, 1))
        return;

    unsigned int b = pad.Buttons;

    key_event(b, PSP_CTRL_CROSS, ACT_BOOST);
    key_event(b, PSP_CTRL_SQUARE, ACT_INTERACT);
    key_event(b, PSP_CTRL_TRIANGLE, ACT_VEHICLE);
    key_event(b, PSP_CTRL_START, ACT_PAUSE);
    key_event(b, PSP_CTRL_HOME, ACT_QUIT);
    key_event(b, PSP_CTRL_SELECT, ACT_DEV_DEBUG);
    key_event(b, PSP_CTRL_RTRIGGER, ACT_DEV_CAM);

    /* Analog steer with a deadzone; D-pad overrides when the nub is centred. */
    float steer = nub_axis(pad.Lx, STEER_DEADZONE);
    if (steer == 0.0f) {
        if (b & PSP_CTRL_RIGHT)
            steer = 1.0f;
        else if (b & PSP_CTRL_LEFT)
            steer = -1.0f;
    }
    input_event_axis(AX_STEER, steer);

    /* No second stick, so the pad has no camera pan; keep the axis at zero. */
    input_event_axis(AX_LOOK_Y, 0.0f);

    if (game_mode() == GAME_MODE_DRIVE) {
        /* Vehicle: the triggers are the throttle/brake (digital, full level);
         * the nub contributes nothing here. */
        input_event_axis(AX_THROTTLE, (b & PSP_CTRL_LTRIGGER) ? 1.0f : 0.0f);
        input_event_axis(AX_BRAKE, (b & PSP_CTRL_RTRIGGER) ? 1.0f : 0.0f);
    } else {
        /* On foot: the stick alone moves — Y = fwd/back (up = forward). */
        float fwd = -nub_axis(pad.Ly, FWD_DEADZONE);
        input_event_axis(AX_THROTTLE, (fwd > 0.0f) ? fwd : 0.0f);
        input_event_axis(AX_BRAKE, (fwd < 0.0f) ? -fwd : 0.0f);
    }

    s_prev_buttons = b;
}
