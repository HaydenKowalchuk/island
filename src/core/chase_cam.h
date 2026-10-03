#ifndef ISLAND_CORE_CHASE_CAM_H
#define ISLAND_CORE_CHASE_CAM_H

#include <cglm/cglm.h>

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
} ChaseCam;

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

void chase_cam_update(ChaseCam* c, const float car_pos[3], float heading,
                      float speed_kmh, int width, int height, float dt);

#endif
