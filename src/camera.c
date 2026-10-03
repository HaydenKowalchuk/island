#include "camera.h"

#include <math.h>

#define CAM_DEFAULT_POS   ((vec3){ 0.0f, 2.0f, 8.0f })
#define CAM_DEFAULT_YAW   (-90.0f)
#define CAM_DEFAULT_PITCH (-10.0f)

static float deg2rad(float deg) {
    return deg * (float)(M_PI / 180.0);
}

/* Unit vector the camera is looking along, derived from yaw/pitch. */
static void cam_forward(const FlyCam* cam, vec3 out) {
    float cy = cosf(deg2rad(cam->yaw));
    float sy = sinf(deg2rad(cam->yaw));
    float cp = cosf(deg2rad(cam->pitch));
    float sp = sinf(deg2rad(cam->pitch));

    out[0] = cy * cp;
    out[1] = sp;
    out[2] = sy * cp;
}

void cam_reset(FlyCam* cam) {
    glm_vec3_copy(CAM_DEFAULT_POS, cam->pos);
    cam->yaw = CAM_DEFAULT_YAW;
    cam->pitch = CAM_DEFAULT_PITCH;
}

void cam_init(FlyCam* cam) {
    cam_reset(cam);
    cam->speed = 10.0f;
    cam->sensitivity = 0.15f;
    cam->fov = 60.0f;
    cam->znear = 0.1f;
    cam->zfar = 1000.0f;

    glm_mat4_identity(cam->view);
    glm_mat4_identity(cam->proj);
    glm_mat4_identity(cam->viewproj);
}

void cam_rotate(FlyCam* cam, float dx, float dy) {
    cam->yaw += dx * cam->sensitivity;
    cam->pitch -= dy * cam->sensitivity;

    if (cam->pitch > 89.0f) cam->pitch = 89.0f;
    if (cam->pitch < -89.0f) cam->pitch = -89.0f;

    if (cam->yaw > 360.0f) cam->yaw -= 360.0f;
    if (cam->yaw < -360.0f) cam->yaw += 360.0f;
}

void cam_move(FlyCam* cam, int forward, int right, int up, float dt) {
    vec3 fwd, right_v, delta, tmp;

    if (!forward && !right && !up) return;

    cam_forward(cam, fwd);
    glm_vec3_crossn(fwd, (vec3){ 0.0f, 1.0f, 0.0f }, right_v);

    glm_vec3_zero(delta);

    if (forward) {
        glm_vec3_scale_as(fwd, (float)forward, tmp);
        glm_vec3_add(delta, tmp, delta);
    }
    if (right) {
        glm_vec3_scale_as(right_v, (float)right, tmp);
        glm_vec3_add(delta, tmp, delta);
    }
    if (up) {
        tmp[0] = 0.0f;
        tmp[1] = (float)up;
        tmp[2] = 0.0f;
        glm_vec3_add(delta, tmp, delta);
    }

    float len = glm_vec3_norm(delta);
    if (len > 0.0f) {
        glm_vec3_scale_as(delta, cam->speed * dt / len, delta);
        glm_vec3_add(cam->pos, delta, cam->pos);
    }
}

void cam_update(FlyCam* cam, int width, int height) {
    vec3 target;

    cam_forward(cam, target);
    glm_vec3_add(cam->pos, target, target);

    /* glm_look() treats its second argument as a direction; use lookat. */
    glm_lookat(cam->pos, target, (vec3){ 0.0f, 1.0f, 0.0f }, cam->view);

    /* cglm's glm_perspective takes the vertical FOV in radians. */
    glm_perspective(glm_rad(cam->fov), (float)width / (float)height, cam->znear, cam->zfar, cam->proj);
    glm_mat4_mul(cam->proj, cam->view, cam->viewproj);
}