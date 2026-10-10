#ifndef ISLAND_CORE_CAR_H
#define ISLAND_CORE_CAR_H

#include <cglm/cglm.h>
#include <stdbool.h>

/*
 * Hover-car physics (island1-9n0n). Car-like planar dynamics: throttle/brake
 * along the heading, lateral grip that can be exceeded to slide, and speed-
 * dependent yaw. Runs on the fixed 1/60 s tick. Pure core: no GL and no input
 * contract -- the caller reads input and passes throttle/brake/steer in.
 *
 * Forward is +Z rotated by heading: fwd = (sin h, 0, cos h). The right-hand
 * screen mapping means positive steer (right) lowers the heading.
 */

typedef struct {
  vec3 pos;        /* world position; y is owned by the game, not physics */
  vec3 vel;        /* world-space horizontal velocity (m/s) */
  float heading;   /* yaw, radians; forward = (sin h, 0, cos h) */
  float yaw_rate;  /* smoothed dHeading/dt */
  float speed_kmh; /* magnitude of horizontal velocity, for HUD/camera */
  bool on_water;   /* true when over open water (terrain <= sea level) */
} CarState;

/* Longitudinal and lateral tuning (m/s^2 and 1/s). */
#define CAR_ACCEL 24.0f      /* throttle acceleration */
#define CAR_BRAKE 30.0f      /* brake / reverse acceleration */
#define CAR_WATER_DRAG 0.85f /* linear drag over water (sluggish) */
#define CAR_LAND_DRAG 0.30f  /* linear drag over land (grippier) */
#define CAR_MAX_FWD 28.0f    /* top speed, m/s (~101 km/h) */
#define CAR_MAX_REV 8.0f     /* top reverse speed, m/s */
#define CAR_GRIP 6.0f        /* lateral velocity decay, 1/s */
#define CAR_YAW_GAIN 1.9f    /* target yaw rate at full lock, rad/s */
#define CAR_YAW_SMOOTH 5.0f  /* yaw-rate ease, 1/s */
#define CAR_DRIFT_REF 18.0f  /* speed at which steering fully breaks grip */

void car_init(CarState* c, float x, float z, float heading);

/*
 * Advance one tick. throttle/brake are absolute 0..1; steer is -1 (left) to
 * +1 (right). ground_y is the terrain height under the car (<= 0 is water).
 */
void car_tick(CarState* c, float throttle, float brake, float steer, float ground_y, float dt);

#endif
