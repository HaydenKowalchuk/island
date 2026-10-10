/*
 * GL-free behaviour test for the hover-car physics (island1-9n0n).
 *
 * Covers the acceptance criteria without a window:
 *   - 0 to ~top speed in ~3.5 s on water, holding a straight line
 *   - land accelerates harder than water (lower drag)
 *   - a hard turn at speed produces a lateral slide that then decays
 *   - reversing works
 *   - on_water flips at the sea-level crossing
 */
#include "car.h"

#include <math.h>
#include <stdio.h>

#define TICK (1.0f / 60.0f)
#define SEA (-1000.0f) /* terrain_height_at off-map == open water */
#define LAND 5.0f

static float lateral_speed(const CarState* c) {
  float fwd[2] = {sinf(c->heading), cosf(c->heading)};
  float right[2] = {fwd[1], -fwd[0]};
  return c->vel[0] * right[0] + c->vel[2] * right[1];
}

int main(void) {
  CarState c;

  /* 1. Straight line and 0-to-top time on water. */
  car_init(&c, 0.0f, 0.0f, 0.0f);
  float t = 0.0f, t_top = -1.0f;
  const float top_kmh = 0.95f * CAR_MAX_FWD * 3.6f;
  for (int i = 0; i < 600; i++) {
    car_tick(&c, 1.0f, 0.0f, 0.0f, SEA, TICK);
    t += TICK;
    if (t_top < 0.0f && c.speed_kmh >= top_kmh)
      t_top = t;
  }
  if (t_top < 2.5f || t_top > 3.8f) {
    printf("FAIL: 0-to-top time=%.2fs\n", t_top);
    return 1;
  }
  if (c.heading != 0.0f || fabsf(c.pos[0]) > 0.01f) {
    printf("FAIL: straight line drifted x=%.3f h=%.3f\n", c.pos[0], c.heading);
    return 1;
  }
  if (!c.on_water || c.pos[2] < 100.0f) {
    printf("FAIL: forward/water pos_z=%.1f on_water=%d\n", c.pos[2], c.on_water);
    return 1;
  }
  if (c.speed_kmh > CAR_MAX_FWD * 3.6f + 0.1f) {
    printf("FAIL: overspeed %.1f km/h\n", c.speed_kmh);
    return 1;
  }

  /* 2. Land is grippier/faster than water at the same throttle. */
  CarState water, land;
  car_init(&water, 0.0f, 0.0f, 0.0f);
  car_init(&land, 0.0f, 0.0f, 0.0f);
  for (int i = 0; i < 120; i++) {
    car_tick(&water, 1.0f, 0.0f, 0.0f, SEA, TICK);
    car_tick(&land, 1.0f, 0.0f, 0.0f, LAND, TICK);
  }
  if (!(land.speed_kmh > water.speed_kmh) || land.on_water || !water.on_water) {
    printf("FAIL: drag land=%.1f water=%.1f\n", land.speed_kmh, water.speed_kmh);
    return 1;
  }

  /* 3. Hard turn at speed: slide, then recover once steering is released. */
  car_init(&c, 0.0f, 0.0f, 0.0f);
  for (int i = 0; i < 300; i++)
    car_tick(&c, 1.0f, 0.0f, 0.0f, SEA, TICK);

  float max_slide = 0.0f;
  for (int i = 0; i < 20; i++) {
    car_tick(&c, 1.0f, 0.0f, 1.0f, SEA, TICK);
    float s = fabsf(lateral_speed(&c));
    if (s > max_slide)
      max_slide = s;
  }
  if (max_slide < 1.0f) {
    printf("FAIL: no slide, max_lateral=%.3f\n", max_slide);
    return 1;
  }
  if (fabsf(c.heading) < 0.05f) {
    printf("FAIL: steering did not turn, h=%.3f\n", c.heading);
    return 1;
  }

  for (int i = 0; i < 180; i++)
    car_tick(&c, 1.0f, 0.0f, 0.0f, SEA, TICK);
  if (fabsf(lateral_speed(&c)) > 0.5f) {
    printf("FAIL: slide did not decay, lateral=%.3f\n", lateral_speed(&c));
    return 1;
  }

  /* 4. Reverse. */
  car_init(&c, 0.0f, 0.0f, 0.0f);
  for (int i = 0; i < 90; i++)
    car_tick(&c, 0.0f, 1.0f, 0.0f, SEA, TICK);
  if (!(c.vel[2] < -1.0f) || !(c.pos[2] < -1.0f) || !(c.speed_kmh > 0.0f)) {
    printf("FAIL: reverse vz=%.2f z=%.2f\n", c.vel[2], c.pos[2]);
    return 1;
  }
  if (c.speed_kmh > CAR_MAX_REV * 3.6f + 0.1f) {
    printf("FAIL: reverse overspeed %.1f\n", c.speed_kmh);
    return 1;
  }
  float rev_kmh = c.speed_kmh;

  /* 5. on_water flips at the sea-level crossing. */
  car_init(&c, 0.0f, 0.0f, 0.0f);
  car_tick(&c, 0.0f, 0.0f, 0.0f, 0.0f, TICK);
  int w0 = c.on_water;
  car_tick(&c, 0.0f, 0.0f, 0.0f, 2.0f, TICK);
  int w1 = c.on_water;
  car_tick(&c, 0.0f, 0.0f, 0.0f, SEA, TICK);
  int w2 = c.on_water;
  if (!(w0 && !w1 && w2)) {
    printf("FAIL: on_water flags %d/%d/%d\n", w0, w1, w2);
    return 1;
  }

  printf("car: 0-top=%.2fs slide_max=%.2f slide_end=%.3f rev=%.1fkm/h\n",
         t_top,
         max_slide,
         fabsf(lateral_speed(&c)),
         rev_kmh);
  return 0;
}
