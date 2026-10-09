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

    /*
     * 4. Aspect handling (island1-t21d): the horizontal FOV must match across
     *    16:9, 4:3 and the PSP's exact 480x272 (1.765:1), and 16:9 must
     *    reproduce the reference vertical FOV.
     *    proj[5] = 1/tan(vfov/2); horizontal = 2*atan(aspect/proj[5]).
     */
    ChaseCam wide, tall, psp;
    chase_cam_init(&wide, car, 0.0f);
    chase_cam_init(&tall, car, 0.0f);
    chase_cam_init(&psp, car, 0.0f);
    for (int i = 0; i < 600; i++) {
        chase_cam_update(&wide, car, 0.0f, 100.0f, 640, 360, TICK); /* 16:9 */
        chase_cam_update(&tall, car, 0.0f, 100.0f, 320, 240, TICK); /* 4:3  */
        chase_cam_update(&psp, car, 0.0f, 100.0f, 480, 272, TICK);  /* PSP  */
    }
    float hfov_wide = 2.0f * atanf((640.0f / 360.0f) / wide.proj[1][1]);
    float hfov_tall = 2.0f * atanf((320.0f / 240.0f) / tall.proj[1][1]);
    float hfov_psp = 2.0f * atanf((480.0f / 272.0f) / psp.proj[1][1]);
    float vfov_wide = 2.0f * atanf(1.0f / wide.proj[1][1]);
    if (fabsf(hfov_wide - hfov_tall) > 0.01f) {
        printf("FAIL: horizontal fov 16:9=%.3f 4:3=%.3f\n", hfov_wide, hfov_tall);
        return 1;
    }
    if (fabsf(hfov_wide - hfov_psp) > 0.01f) {
        printf("FAIL: horizontal fov 16:9=%.3f psp=%.3f\n", hfov_wide, hfov_psp);
        return 1;
    }
    if (fabsf(vfov_wide - glm_rad(fast.fov)) > 0.01f) {
        printf("FAIL: 16:9 vfov=%.3f ref=%.3f\n", vfov_wide, glm_rad(fast.fov));
        return 1;
    }

    /*
     * 5. Transition without a cut (island1-38h3): the spring seeds from
     *    `from_pos` (NULL snaps, like init) and pulls to the framing; the
     *    distance to the target shrinks monotonically (no overshoot).
     */
    ChaseCam tr;
    const float from[3] = {0.0f, 3.0f, -30.0f};
    chase_cam_transition(&tr, car, 0.0f, from);
    float seeded = sqrtf(tr.pos[0] * tr.pos[0] + tr.pos[1] * tr.pos[1] +
                         (tr.pos[2] + 30.0f) * (tr.pos[2] + 30.0f));
    if (seeded > 0.001f || fabsf(tr.heading_lag) > 1e-6f || vel_norm(&tr) > 1e-6f) {
        printf("FAIL: transition seed pos=(%.2f, %.2f, %.2f) lag=%.3f\n",
               tr.pos[0], tr.pos[1], tr.pos[2], tr.heading_lag);
        return 1;
    }
    float prev_d = 1.0e9f;
    float max_rise = 0.0f;
    for (int i = 0; i < 300; i++) {
        chase_cam_update(&tr, car, 0.0f, 0.0f, 320, 240, TICK);
        float d = sqrtf(tr.pos[0] * tr.pos[0] +
                        (tr.pos[1] - 3.2f) * (tr.pos[1] - 3.2f) +
                        (tr.pos[2] + 7.0f) * (tr.pos[2] + 7.0f));
        if (d > prev_d && d - prev_d > max_rise)
            max_rise = d - prev_d;
        prev_d = d;
    }
    if (max_rise > 0.01f || prev_d > 0.5f) {
        printf("FAIL: transition lerp rise=%.4f final_d=%.3f\n", max_rise,
               prev_d);
        return 1;
    }

    /* NULL from_pos snaps into place, identical to init. */
    ChaseCam snap;
    chase_cam_transition(&snap, car, 0.0f, NULL);
    if (fabsf(snap.pos[2] + 7.0f) > 0.25f || fabsf(snap.pos[1] - 3.2f) > 0.25f) {
        printf("FAIL: NULL transition snap (%.2f, %.2f, %.2f)\n", snap.pos[0],
               snap.pos[1], snap.pos[2]);
        return 1;
    }

    printf("chase-cam: rest_vel=%.4f settle=%.3fs fov %.1f->%.1f z %.1f->%.1f "
           "hfov %.3f/%.3f/%.3f\n",
           rest_vel, settle_time, slow.fov, fast.fov, slow.pos[2], fast.pos[2],
           hfov_wide, hfov_tall, hfov_psp);
    return 0;
}
