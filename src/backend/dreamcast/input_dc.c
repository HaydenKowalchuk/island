#include "input_dc.h"

#include <math.h>

#include <kos.h>

#include "game.h"  /* game_mode(): the mapping is mode-aware */
#include "input.h" /* core input contract */

/*
 * Dreamcast binding table (island1-19s3, mode-aware). This is where maple
 * controller hardware is mapped onto the platform-agnostic Action/axis
 * contract via the same input_event_* functions the desktop backend calls.
 *
 *   R lever      -> throttle (vehicle, analog lever 0..1)
 *   L lever      -> brake    (vehicle, analog lever 0..1)
 *   A (green)    -> boost
 *   Y (red)      -> interact (pickup / deliver / talk)
 *   X (yellow)   -> vehicle (enter / exit)
 *   analog stick -> AX_STEER (x); on foot also fwd/back (y -> AX_THROTTLE /
 *                   AX_BRAKE), so the walking player moves on the stick
 *                   alone; D-pad steers when the stick is centred
 *   Start        -> pause
 *   A+B+X+Y+Start -> quit (KOS convention for exiting)
 *   B            -> ACT_DEV_CAM (dev fly cam; the only free face button)
 *
 * R accelerates and L brakes, matching the usual console convention (Xbox and
 * PlayStation pads put the accelerator on the right trigger).
 *
 * No mouse: ACT_LOOK stays 0, chase cam only. Nothing is bound to Z/D/C - those
 * exist only on a VMU, and KOS inverts the raw button word, so a bit a standard
 * pad never drives can read back as held.
 *
 * The vehicle is driven ONLY by the analog levers; on foot the stick alone
 * moves. The throttle/brake axes feed the core's fmaxf(axis, held) reads, so
 * the analog lever value survives untouched (no threshold jumps). The levers
 * also raise the digital ACT_THROTTLE/ACT_BRAKE fallback the core keeps for
 * menus/dev (island1-553a) - the dev free-fly cam reads those, and the axes
 * alone would leave it unable to move.
 */
#define NUB_RANGE 127.0f
/* The stick is sensitive; remaining travel is remapped to 0..1 so full
 * deflection still reaches a full-value axis. */
#define STEER_DEADZONE 0.30f
#define FWD_DEADZONE 0.30f
/* Past this the lever counts as pulled. A standard pad reports 0 or 255; a
 * VMU reports the true analog position. */
#define TRIGGER_THRESHOLD 128

/* Digital button edges are emitted only on change, like GLFW's key callback. */
static unsigned int s_prev_buttons;
static int s_prev_quit;

/* Every action this table raises, so an absent pad can release all of them. */
static const Action s_mapped[] = {
    ACT_THROTTLE, ACT_BRAKE, ACT_BOOST, ACT_INTERACT, ACT_VEHICLE,
    ACT_PAUSE, ACT_QUIT, ACT_DEV_CAM,
};

static void key_event(unsigned int buttons, unsigned int mask, Action action) {
    int now = (buttons & mask) ? 1 : 0;
    int was = (s_prev_buttons & mask) ? 1 : 0;
    if (now != was)
        input_event_key(action, now);
}

/*
 * Signed stick axis (-1..1) with a deadzone; remaining travel remapped.
 * KOS already subtracts 128 from the raw joyx/joyy (controller.c:176), so a
 * centred stick reads 0. The parameter is int because that is the width of
 * cont_state_t's fields.
 */
static float stick_axis(int raw, float deadzone) {
    float v = (float)raw / NUB_RANGE;
    float a = fabsf(v);
    if (a < deadzone)
        return 0.0f;
    float scaled = (a - deadzone) / (1.0f - deadzone);
    return (v < 0.0f) ? -scaled : scaled;
}

/*
 * Levers double as the digital throttle/brake fallback the core keeps for
 * menus/dev (island1-553a). `active` says whether this poll drives them: on
 * foot the actions are released (the stick moves the player instead) but the
 * edge trackers still move, so grabbing the car with a lever already pulled
 * produces a fresh press rather than nothing.
 */
static int s_prev_throttle;
static int s_prev_brake;

static void update_levers(int throttle_raw, int brake_raw, int active) {
    /* R accelerates, L brakes (the usual console convention) - do not let
     * these two get transposed. */
    int want_throttle = active && throttle_raw > TRIGGER_THRESHOLD;
    int want_brake = active && brake_raw > TRIGGER_THRESHOLD;

    if (want_throttle != s_prev_throttle) {
        input_event_key(ACT_THROTTLE, want_throttle);
        s_prev_throttle = want_throttle;
    }
    if (want_brake != s_prev_brake) {
        input_event_key(ACT_BRAKE, want_brake);
        s_prev_brake = want_brake;
    }
}

/*
 * Release everything and centre every axis. Axes are absolute levels that
 * survive until overwritten, so a controller that stops reporting (unplugged)
 * must not leave the throttle stuck on.
 */
static void neutral_input(void) {
    for (size_t i = 0; i < sizeof(s_mapped) / sizeof(s_mapped[0]); i++)
        input_event_key(s_mapped[i], false);
    input_event_axis(AX_STEER, 0.0f);
    input_event_axis(AX_THROTTLE, 0.0f);
    input_event_axis(AX_BRAKE, 0.0f);
    s_prev_buttons = 0;
    s_prev_quit = 0;
    s_prev_throttle = 0;
    s_prev_brake = 0;
}

void input_dc_init(void) {
    backend_input_init();
}

void backend_input_init(void) {
    s_prev_buttons = 0;
    s_prev_quit = 0;
    s_prev_throttle = 0;
    s_prev_brake = 0;
}

void backend_input_poll(void) {
    maple_device_t* cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if (!cont) {
        neutral_input();
        return;
    }
    cont_state_t* st = (cont_state_t*)maple_dev_status(cont);
    if (!st) {
        neutral_input();
        return;
    }

    unsigned int buttons = st->buttons;
    GameMode mode = game_mode();

    /* Mode-independent context actions. A = boost (the R lever is the
     * vehicle's throttle now). */
    key_event(buttons, CONT_A, ACT_BOOST);
    key_event(buttons, CONT_Y, ACT_INTERACT);
    key_event(buttons, CONT_X, ACT_VEHICLE);
    key_event(buttons, CONT_START, ACT_PAUSE);
    key_event(buttons, CONT_B, ACT_DEV_CAM);

    /* Stick X steers in both modes; D-pad overrides when the stick is
     * centred. joyx is -128..127 and already centred on 0 by KOS. */
    float steer = stick_axis(st->joyx, STEER_DEADZONE);
    if (steer == 0.0f) {
        if (buttons & CONT_DPAD_RIGHT)
            steer = 1.0f;
        else if (buttons & CONT_DPAD_LEFT)
            steer = -1.0f;
    }
    input_event_axis(AX_STEER, steer);

    /* R accelerates, L brakes. */
update_levers(st->rtrig, st->ltrig, mode == GAME_MODE_DRIVE);

    if (mode == GAME_MODE_DRIVE) {
        /* Vehicle: the L/R levers are the throttle/brake, analog 0..1. */
        input_event_axis(AX_THROTTLE, (float)st->rtrig / 255.0f);
        input_event_axis(AX_BRAKE, (float)st->ltrig / 255.0f);
    } else {
        /* On foot: the stick alone moves - Y = fwd/back (up = forward). */
        float fwd = -stick_axis(st->joyy, FWD_DEADZONE);
        input_event_axis(AX_THROTTLE, (fwd > 0.0f) ? fwd : 0.0f);
        input_event_axis(AX_BRAKE, (fwd < 0.0f) ? -fwd : 0.0f);
    }

    /* Exit combo: all face buttons + Start. */
    unsigned int combo = CONT_A | CONT_B | CONT_X | CONT_Y | CONT_START;
    int quit_now = (buttons & combo) == combo;
    if (quit_now != s_prev_quit)
        input_event_key(ACT_QUIT, quit_now);
    s_prev_quit = quit_now;

    s_prev_buttons = buttons;
}
