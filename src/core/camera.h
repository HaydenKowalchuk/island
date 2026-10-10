#ifndef ISLAND_CORE_CAMERA_H
#define ISLAND_CORE_CAMERA_H

#include <stdbool.h>
#include <cglm/cglm.h>

typedef struct {
    vec3 pos;
    float yaw;   /* degrees, 0 = +X, increases toward +Z */
    float pitch; /* degrees, clamped to (-89, 89) */

    float speed;
    float sensitivity;
    float fov;   /* vertical, degrees */
    float znear;
    float zfar;

    mat4 view;
    mat4 proj;
    mat4 viewproj;
} FlyCam;

void cam_init(FlyCam* cam);
void cam_reset(FlyCam* cam);
void cam_rotate(FlyCam* cam, float dx, float dy);
void cam_move(FlyCam* cam, int forward, int right, int up, float dt);
void cam_update(FlyCam* cam, int width, int height);

/*
 * The pose the fly cam renders right now: eye, a look-at point 10m along its
 * forward direction, and fov. For camera transitions (island1-38h3).
 */
void cam_pose(const FlyCam* cam, float pos[3], float look[3], float* fov);

#endif
