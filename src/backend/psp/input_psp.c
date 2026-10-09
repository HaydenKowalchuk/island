#include <math.h>

#include <pspctrl.h>

#include "game.h"  /* game_mode(): the mapping is mode-aware */
#include "input.h" /* core input contract */

/*
 * PSP controller binding table (island1-rggc, mode-aware). The pad is digital
 * plus a small analog nub; there is no mouse, so ACT_LOOK is never held and
 * the chase camera stays automatic.
 *
 *   R        -> throttle (vehicle, digital)   L  -> brake (vehicle, digital)
 *   Cross    -> boost                             Square -> ACT_INTERACT
 *   Triangle -> ACT_VEHICLE                       Start  -> ACT_PAUSE
 *   Select   -> ACT_DEV_DEBUG                     Home   -> ACT_QUIT
 *   D-pad Up -> inventory overlay (island1-iim2)
 *   Vol+ / Vol- -> ACT_DEV_CAM / ACT_DEV_RESET
 *   nub X    -> AX_STEER; nub Y -> fwd/back on foot only (AX_THROTTLE /
 *              AX_BRAKE), so the walking player moves on the stick alone;
 *              D-pad steers when the nub is centred.
 *
 * R accelerates and L brakes, matching the usual console convention (Xbox and
 * PlayStation pads put the accelerator on the right trigger).
 *
 * The vehicle is driven ONLY by the triggers; on foot the stick alone moves.
 * In vehicle mode the triggers also raise the digital ACT_THROTTLE/ACT_BRAKE
 * fallback the core keeps for menus/dev (island1-553a) - the dev free-fly cam
 * reads those, and the axes alone would leave it unable to move.
 */

#define NUB_CENTER 128
#define NUB_RANGE 127
/* Large deadzone (the nub is very sensitive); the remaining travel is remapped
 * to 0..1 so full deflection still reaches a full-value axis. */
#define STEER_DEADZONE 0.30f
#define FWD_DEADZONE 0.30f

/* Digital button edges are emitted only on change, like GLFW's key callback. */
static unsigned int s_prev_buttons;

/* Emitted state of the digital throttle/brake actions, see update_triggers. */
static int s_prev_throttle;
static int s_prev_brake;

/* Every action this table raises, so a failed poll can release all of them. */
static const Action s_mapped[] = {
    ACT_THROTTLE, ACT_BRAKE,     ACT_BOOST,     ACT_INTERACT,
    ACT_VEHICLE,  ACT_PAUSE,     ACT_INVENTORY, ACT_QUIT,
    ACT_DEV_CAM,  ACT_DEV_DEBUG, ACT_DEV_RESET,
};

static void key_event(unsigned int buttons, unsigned int mask, Action action) {
  int now = (buttons & mask) ? 1 : 0;
  int was = (s_prev_buttons & mask) ? 1 : 0;
  if (now != was)
    input_event_key(action, now);
}

/*
 * The triggers double as the digital throttle/brake fallback the core keeps
 * for menus/dev (island1-553a). `active` says whether this poll drives them:
 * on foot the actions are released (the stick moves the player instead) but
 * the edge trackers still move, so grabbing the car with a trigger already
 * held produces a fresh press rather than nothing.
 */
static void update_triggers(unsigned int buttons, int active) {
  /* Right trigger accelerates, left trigger brakes (the usual console
   * convention) - do not let these two get transposed. */
  int want_throttle = active && (buttons & PSP_CTRL_RTRIGGER);
  int want_brake = active && (buttons & PSP_CTRL_LTRIGGER);

  if (want_throttle != s_prev_throttle) {
    input_event_key(ACT_THROTTLE, want_throttle);
    s_prev_throttle = want_throttle;
  }
  if (want_brake != s_prev_brake) {
    input_event_key(ACT_BRAKE, want_brake);
    s_prev_brake = want_brake;
  }
}

/* Set every action up and centre every axis. Axes are absolute levels that
 * survive until they are overwritten, so a pad that stops reporting (unplugged,
 * peek failure) must not leave the last throttle stuck on. */
static void neutral_input(void) {
  for (size_t i = 0; i < sizeof(s_mapped) / sizeof(s_mapped[0]); i++)
    input_event_key(s_mapped[i], false);
  input_event_axis(AX_STEER, 0.0f);
  input_event_axis(AX_THROTTLE, 0.0f);
  input_event_axis(AX_BRAKE, 0.0f);
  input_event_axis(AX_LOOK_Y, 0.0f);
  s_prev_buttons = 0;
  s_prev_throttle = 0;
  s_prev_brake = 0;
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
  s_prev_throttle = 0;
  s_prev_brake = 0;
  sceCtrlSetSamplingCycle(0);
  sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
}

void backend_input_poll(void) {
  SceCtrlData pad;
  if (!sceCtrlPeekBufferPositive(&pad, 1)) {
    neutral_input();
    return;
  }

  unsigned int b = pad.Buttons;

  key_event(b, PSP_CTRL_SQUARE, ACT_INTERACT);
  key_event(b, PSP_CTRL_CROSS, ACT_BOOST);
  key_event(b, PSP_CTRL_TRIANGLE, ACT_VEHICLE);
  key_event(b, PSP_CTRL_START, ACT_PAUSE);
  key_event(b, PSP_CTRL_HOME, ACT_QUIT);
  // key_event(b, PSP_CTRL_SELECT, ACT_DEV_DEBUG);
  key_event(b, PSP_CTRL_SELECT, ACT_INVENTORY);
  /* Dev bindings live on the volume keys: the triggers carry the vehicle
   * throttle/brake now, and the face buttons all carry gameplay actions. */
  key_event(b, PSP_CTRL_VOLUP, ACT_DEV_CAM);
  key_event(b, PSP_CTRL_VOLDOWN, ACT_DEV_RESET);

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

  int driving = game_mode() == GAME_MODE_DRIVE;
  update_triggers(b, driving);

  if (driving) {
    /* Vehicle: the triggers are the throttle/brake (digital, full level);
     * the nub contributes nothing here. R accelerates, L brakes. */
    input_event_axis(AX_THROTTLE, (b & PSP_CTRL_RTRIGGER) ? 1.0f : 0.0f);
    input_event_axis(AX_BRAKE, (b & PSP_CTRL_LTRIGGER) ? 1.0f : 0.0f);
  } else {
    /* On foot: the stick alone moves - Y = fwd/back (up = forward). */
    float fwd = -nub_axis(pad.Ly, FWD_DEADZONE);
    input_event_axis(AX_THROTTLE, (fwd > 0.0f) ? fwd : 0.0f);
    input_event_axis(AX_BRAKE, (fwd < 0.0f) ? -fwd : 0.0f);
  }

  s_prev_buttons = b;
}
