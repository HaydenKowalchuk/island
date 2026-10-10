/* GL-free test for the platform-agnostic input contract (island1-ibqg). */
#include "input.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static int feq(float a, float b) {
  return fabsf(a - b) < 1e-6f;
}

int main(void) {
  input_init();

  /* A press is an edge on exactly one tick, then held. */
  input_event_key(ACT_INTERACT, true);
  input_begin_frame();
  assert(input_pressed(ACT_INTERACT));
  assert(input_held(ACT_INTERACT));
  assert(!input_released(ACT_INTERACT));

  input_begin_frame();
  assert(!input_pressed(ACT_INTERACT));
  assert(input_held(ACT_INTERACT));

  input_event_key(ACT_INTERACT, false);
  input_begin_frame();
  assert(!input_held(ACT_INTERACT));
  assert(input_released(ACT_INTERACT));
  assert(!input_pressed(ACT_INTERACT));

  /* A tap between two ticks still yields one press and one release. */
  input_event_key(ACT_VEHICLE, true);
  input_event_key(ACT_VEHICLE, false);
  input_begin_frame();
  assert(input_pressed(ACT_VEHICLE));
  assert(input_released(ACT_VEHICLE));
  assert(!input_held(ACT_VEHICLE));

  /* Steer is an absolute level: stable across ticks until changed. */
  input_event_axis(AX_STEER, -1.0f);
  input_begin_frame();
  assert(feq(input_axis(AX_STEER), -1.0f));
  input_begin_frame();
  assert(feq(input_axis(AX_STEER), -1.0f));
  input_event_axis(AX_STEER, 1.0f);
  input_begin_frame();
  assert(feq(input_axis(AX_STEER), 1.0f));

  /* Throttle/brake are absolute analog levels too (triggers on consoles). */
  input_event_axis(AX_THROTTLE, 0.4f);
  input_event_axis(AX_BRAKE, 0.0f);
  input_begin_frame();
  assert(feq(input_axis(AX_THROTTLE), 0.4f));
  assert(feq(input_axis(AX_BRAKE), 0.0f));
  input_event_axis(AX_THROTTLE, 0.0f);
  input_event_axis(AX_BRAKE, 0.7f);
  input_begin_frame();
  assert(feq(input_axis(AX_THROTTLE), 0.0f));
  assert(feq(input_axis(AX_BRAKE), 0.7f));

  /* Look is a per-tick delta: consumed once, then zero. */
  input_event_mouse(2.0f, 3.0f);
  input_begin_frame();
  assert(feq(input_axis(AX_LOOK_X), 2.0f));
  assert(feq(input_axis(AX_LOOK_Y), 3.0f));
  input_begin_frame();
  assert(feq(input_axis(AX_LOOK_X), 0.0f));
  assert(feq(input_axis(AX_LOOK_Y), 0.0f));

  printf("input: edges + axes OK\n");
  return 0;
}
