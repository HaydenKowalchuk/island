#ifndef ISLAND_CORE_CHASE_CAM_H
#define ISLAND_CORE_CHASE_CAM_H

#include <cglm/cglm.h>

/* A camera pose for a transition: eye, look-at point, 16:9-referenced fov. */
typedef struct {
  vec3 pos;
  vec3 look;
  float fov;
} CamPose;

/*
 * Third-person chase camera (island1-fnss). A critically-damped spring follows a
 * point behind the car, while the camera's heading eases toward the car's --
 * the signature PS1 "camera catches up" feel. Integrate per *render* frame with
 * the frame dt (not the fixed tick), then read view/proj.
 */
typedef struct {
  vec3 pos, vel;     /* spring state */
  float heading_lag; /* eased car heading, radians */
  float fov;         /* reference FOV at 16:9 (vertical there), degrees */
  mat4 view;
  mat4 proj;

  /* Scripted arc transition (island1-38h3): plays instead of the spring
   * until it ends, then the spring takes over seamlessly (the arc's S-curve
   * ends at zero velocity, so there is no seam). */
  int trans;     /* arc in progress */
  float trans_t; /* seconds into the window */
  CamPose from;  /* camera pose at the start */
} ChaseCam;

/*
 * Re-aim at a new target without a cut (island1-38h3): instead of a jump, the
 * camera plays a ~1.2s arc from `from` to the chase framing around `subject`.
 * It swings to the bearing and height first (lateral + vertical) and closes
 * the distance late, eased on an S-curve (slow -> fast -> slow) with the FOV
 * blending alongside. `from` NULL snaps into place (boot, before any camera
 * exists — identical to chase_cam_init).
 */
void chase_cam_transition(ChaseCam* c, const float subject[3], float heading, const CamPose* from);

/* Snap into the framing behind the target (boot, reset). */
void chase_cam_init(ChaseCam* c, const float car_pos[3], float heading);

/*
 * Optional terrain-height callback (island1-qvn1): return a large negative value
 * off the map. The camera is kept at least a little above the ground so it does
 * not bury itself in a hill. Passing NULL disables the clamp, which is what the
 * GL-free chase-cam test relies on.
 */
typedef float (*ChaseGroundFn)(float x, float z);
void chase_cam_set_ground(ChaseGroundFn fn);

/*
 * Camera framing (island1-zku4): base distance/height behind the target and the
 * height of the look-at point. Defaults match the vehicle; on foot a closer,
 * lower rig reads better.
 */
void chase_cam_set_framing(float dist, float height, float look_height);

/*
 * Vertical FOV in RADIANS that preserves the 16:9 horizontal FOV at `aspect`
 * (island1-t21d). Shared with the attract camera (island1-7u5i).
 */
float chase_vfov_for_aspect(float ref_vfov_deg, float aspect);

void chase_cam_update(ChaseCam* c, const float car_pos[3], float heading, float speed_kmh, int width, int height, float dt);

#endif
