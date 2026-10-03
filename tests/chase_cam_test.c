/*
 * GL-free behaviour test for the chase camera (island1-fnss).
 *
 * Checks the acceptance criteria without a window:
 *   - at rest the spring settles with no residual jitter
 *   - a hard turn eases the camera heading in without overshoot
 *   - higher speed pulls the camera back and widens the FOV
 */
#include <math.h>
#include <stdio.h>

#include "chase_cam.h"

#define TICK (1.0f / 60.0f)

static float vel_norm(const ChaseCam* c) {
    return sqrtf(c->vel[0] * c->vel[0] + c->vel[1] * c->vel[1] + c->vel[2] * c->vel[2]);
}

int main(void) {
    float car[3] = { 0.0f, 0.0f, 0.0f };
    ChaseCam c;

    /* 1. Settle at rest. */
    chase_cam_init(&c, car, 0.0f);
    for (int i = 0; i < 300; i++)
        chase_cam_update(&c, car, 0.0f, 0.0f, 320, 240, TICK);

    float rest_vel = vel_norm(&c);
    if (rest_vel > 0.02f) {
        printf("FAIL: rest jitter, vel=%.4f\n", rest_vel);
        return 1;
    }
    if (fabsf(c.pos[2] + 7.0f) > 0.25f || fabsf(c.pos[1] - 3.2f) > 0.25f) {
        printf("FAIL: rest pose (%.2f, %.2f, %.2f)\n", c.pos[0], c.pos[1], c.pos[2]);
        return 1;
    }

    /* 2. Hard turn: bump the car heading and watch the camera ease. */
    const float target_heading = 1.0f;
    float settle_time = -1.0f;
    float max_overshoot = 0.0f;
    float t = 0.0f;
    for (int i = 0; i < 240; i++) {
        chase_cam_update(&c, car, target_heading, 0.0f, 320, 240, TICK);
        t += TICK;

        float err = target_heading - c.heading_lag;
        if (err < 0.0f && -err > max_overshoot)
            max_overshoot = -err;
        if (settle_time < 0.0f && fabsf(err) < 0.05f)
            settle_time = t;
    }
    if (settle_time < 0.1f || settle_time > 0.7f) {
        printf("FAIL: heading settle time=%.3f\n", settle_time);
        return 1;
    }
    if (max_overshoot > 0.05f) {
        printf("FAIL: heading overshoot=%.3f\n", max_overshoot);
        return 1;
    }

    /* 3. Speed pulls the camera back and widens the FOV. */
    ChaseCam slow, fast;
    chase_cam_init(&slow, car, 0.0f);
    chase_cam_init(&fast, car, 0.0f);
    for (int i = 0; i < 600; i++) {
        chase_cam_update(&slow, car, 0.0f, 0.0f, 320, 240, TICK);
        chase_cam_update(&fast, car, 0.0f, 100.0f, 320, 240, TICK);
    }
    if (!(fast.fov > slow.fov)) {
        printf("FAIL: fov slow=%.1f fast=%.1f\n", slow.fov, fast.fov);
        return 1;
    }
    if (!(fast.pos[2] < slow.pos[2] - 1.0f)) {
        printf("FAIL: no pull-back slow_z=%.2f fast_z=%.2f\n", slow.pos[2], fast.pos[2]);
        return 1;
    }

    printf("chase-cam: rest_vel=%.4f settle=%.3fs fov %.1f->%.1f z %.1f->%.1f\n",
           rest_vel, settle_time, slow.fov, fast.fov, slow.pos[2], fast.pos[2]);
    return 0;
}
