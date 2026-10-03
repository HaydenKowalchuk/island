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
    float fov;         /* current vertical FOV, degrees */
    mat4 view;
    mat4 proj;
} ChaseCam;

void chase_cam_init(ChaseCam* c, const float car_pos[3], float heading);
void chase_cam_update(ChaseCam* c, const float car_pos[3], float heading,
                      float speed_kmh, int width, int height, float dt);

#endif
