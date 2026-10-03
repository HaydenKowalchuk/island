#include <pspctrl.h>

#include "input.h" /* core input contract */

/*
 * PSP controller binding table (island1-rggc). The pad is digital plus a small
 * analog nub; there is no mouse, so ACT_LOOK is never held and the chase camera
 * stays automatic.
 *
 *   Cross    -> ACT_THROTTLE (and AX_THROTTLE)   Circle -> ACT_BRAKE (AX_BRAKE)
 *   L        -> ACT_BOOST                         Square -> ACT_INTERACT
 *   Triangle -> ACT_VEHICLE                       Start  -> ACT_PAUSE
 *   Select   -> ACT_DEV_DEBUG                     Home   -> ACT_QUIT
 *   nub X    -> AX_STEER, nub Y -> AX_LOOK_Y (pan); D-pad steers when centred
 */

#define NUB_CENTER 128
#define NUB_RANGE 127
/* ~0.16 of full deflection; below this the nub reads as centred. */
#define STEER_DEADZONE 0.16f
#define LOOK_DEADZONE 0.20f

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
    if (v > -deadzone && v < deadzone)
        return 0.0f;
    if (v < -1.0f)
        v = -1.0f;
    if (v > 1.0f)
        v = 1.0f;
    return v;
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

    key_event(b, PSP_CTRL_CROSS, ACT_THROTTLE);
    key_event(b, PSP_CTRL_CIRCLE, ACT_BRAKE);
    key_event(b, PSP_CTRL_LTRIGGER, ACT_BOOST);
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

    /* Camera pan rate; zero when centred, accumulated by the input manager. */
    input_event_axis(AX_LOOK_Y, nub_axis(pad.Ly, LOOK_DEADZONE));

    /* Digital throttle/brake on the analog axes the car physics consumes. */
    input_event_axis(AX_THROTTLE, (b & PSP_CTRL_CROSS) ? 1.0f : 0.0f);
    input_event_axis(AX_BRAKE, (b & PSP_CTRL_CIRCLE) ? 1.0f : 0.0f);

    s_prev_buttons = b;
}
