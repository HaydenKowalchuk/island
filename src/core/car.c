#include "car.h"

#include <math.h>

void car_init(CarState* c, float x, float z, float heading) {
  glm_vec3_zero(c->pos);
  glm_vec3_zero(c->vel);
  c->pos[0] = x;
  c->pos[2] = z;
  c->heading = heading;
  c->yaw_rate = 0.0f;
  c->speed_kmh = 0.0f;
  c->on_water = true;
}

void car_tick(CarState* c, float throttle, float brake, float steer, float ground_y, float dt) {
  c->on_water = (ground_y <= 0.0f);

  /* Basis for the current heading. Kept from before the yaw update so the
   * world velocity does not rotate with the nose -- that is what lets a hard
   * turn break traction and slide. */
  float fwd[2] = {sinf(c->heading), cosf(c->heading)};
  float right[2] = {fwd[1], -fwd[0]};

  float v_f = c->vel[0] * fwd[0] + c->vel[2] * fwd[1];
  float v_r = c->vel[0] * right[0] + c->vel[2] * right[1];

  /* Longitudinal: throttle, brake/reverse, then surface drag. */
  v_f += (throttle * CAR_ACCEL - brake * CAR_BRAKE) * dt;
  v_f -= (c->on_water ? CAR_WATER_DRAG : CAR_LAND_DRAG) * v_f * dt;
  if (v_f > CAR_MAX_FWD)
    v_f = CAR_MAX_FWD;
  if (v_f < -CAR_MAX_REV)
    v_f = -CAR_MAX_REV;

  /* Lateral grip -- the drift knob. Hard steering at speed scrubs less of the
   * sideways velocity, so the car keeps sliding until the wheels catch up. */
  float grip = CAR_GRIP * dt;
  if (grip > 1.0f)
    grip = 1.0f;
  float drift = fabsf(steer) * fabsf(v_f) / CAR_DRIFT_REF;
  if (drift > 1.0f)
    drift = 1.0f;
  v_r -= v_r * grip * (1.0f - 0.7f * drift);

  /* Recombine in the pre-yaw basis, then turn the nose toward the steer. */
  c->vel[0] = fwd[0] * v_f + right[0] * v_r;
  c->vel[2] = fwd[1] * v_f + right[1] * v_r;

  float ramp = fabsf(v_f) / 8.0f;
  if (ramp > 1.0f)
    ramp = 1.0f;
  float yaw_target = -steer * CAR_YAW_GAIN * ramp;
  c->yaw_rate +=
      (yaw_target - c->yaw_rate) * (1.0f - expf(-CAR_YAW_SMOOTH * dt));
  c->heading += c->yaw_rate * dt;

  c->pos[0] += c->vel[0] * dt;
  c->pos[2] += c->vel[2] * dt;

  c->speed_kmh = sqrtf(c->vel[0] * c->vel[0] + c->vel[2] * c->vel[2]) * 3.6f;
}
