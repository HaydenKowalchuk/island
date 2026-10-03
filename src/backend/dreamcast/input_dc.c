#include "input_dc.h"

#include <math.h>

#include <kos.h>

#include "input.h" /* core input contract */

/*
 * Dreamcast binding table (island1-19s3's provisional mapping). This is where
 * maple controller hardware is mapped onto the platform-agnostic Action/axis
 * contract via the same input_event_* functions the desktop backend calls.
 *
 *   A (green)      -> throttle (+ AX_THROTTLE full level)
 *   B (blue)       -> brake    (+ AX_BRAKE full level)
 *   L trigger      -> boost    (analog fine: held past half pull)
 *   Y (red)        -> interact (pickup / deliver / talk)
 *   X (yellow)     -> vehicle  (enter / exit)
 *   analog stick   -> AX_STEER (deadzone 0.25)
 *   D-pad          -> steer override when the stick is in the deadzone
 *   Start          -> pause
 *   A+B+X+Y+Start  -> quit (KOS convention for exiting)
 *
 * No mouse: ACT_LOOK stays 0, chase cam only. Dev keys (F3 stats, fly cam)
 * have no binding; add one when needed.
 */
#define DC_DEADZONE 0.25f

void input_dc_init(void) {
    backend_input_init();
}

void backend_input_poll(void) {
    maple_device_t* cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if (!cont)
        return;
    cont_state_t* st = (cont_state_t*)maple_dev_status(cont);
    if (!st)
        return;

    unsigned int buttons = st->buttons;

    input_event_key(ACT_THROTTLE, (buttons & CONT_A) != 0);
    input_event_key(ACT_BRAKE, (buttons & CONT_B) != 0);
    input_event_key(ACT_INTERACT, (buttons & CONT_Y) != 0);
    input_event_key(ACT_VEHICLE, (buttons & CONT_X) != 0);
    input_event_key(ACT_PAUSE, (buttons & CONT_START) != 0);
    input_event_key(ACT_BOOST, st->ltrig > 128);

    /* Analog stick -> steer, deadzone-scaled; D-pad overrides when the stick
     * is in the deadzone (island1-19s3). joyx is a signed byte, -128..127. */
    float jx = (float)st->joyx / 127.0f;
    float steer = 0.0f;
    if (jx > DC_DEADZONE)
        steer = (jx - DC_DEADZONE) / (1.0f - DC_DEADZONE);
    else if (jx < -DC_DEADZONE)
        steer = -((-jx - DC_DEADZONE) / (1.0f - DC_DEADZONE));

    if (buttons & CONT_DPAD_LEFT)
        steer = -1.0f;
    if (buttons & CONT_DPAD_RIGHT)
        steer = 1.0f;
    input_event_axis(AX_STEER, steer);

    /* A/B feed the analog throttle/brake axes at full level, alongside the
     * digital actions (same shape as the desktop W/S mapping). */
    input_event_axis(AX_THROTTLE, (buttons & CONT_A) ? 1.0f : 0.0f);
    input_event_axis(AX_BRAKE, (buttons & CONT_B) ? 1.0f : 0.0f);

    /* Exit combo: all face buttons + Start. */
    unsigned int combo = CONT_A | CONT_B | CONT_X | CONT_Y | CONT_START;
    if ((buttons & combo) == combo)
        input_event_key(ACT_QUIT, 1);
}
