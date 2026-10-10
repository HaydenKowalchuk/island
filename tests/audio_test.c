/*
 * GL-free test for the audio core (island1-850o): positional math, the voice
 * node pool with reserved-loop protection, bus re-application, and the
 * headless no-op contract that keeps the suite device-free.
 */
#include "audio.h"

#include <math.h>
#include <stdio.h>

static int fails = 0;

static void check(int ok, const char* what) {
  if (!ok) {
    printf("FAIL: %s\n", what);
    fails = 1;
  }
}

static int near(float a, float b, float eps) {
  return fabsf(a - b) <= eps;
}

int main(void) {
  /* ---- Positional math ------------------------------------------------ */

  float listener[3] = {0.0f, 0.0f, 0.0f};
  float lvel[3] = {0.0f, 0.0f, 0.0f};
  float vol, pan, pitch;

  /* Directly ahead (heading 0 faces +z), inside the near radius: full
   * volume, centered, no shift. */
  float ahead[3] = {0.0f, 0.0f, 2.0f};
  float zero[3] = {0.0f, 0.0f, 0.0f};
  audio_calc_positional(listener, 0.0f, lvel, ahead, zero, 1.0f, &vol, &pan,
                        &pitch);
  check(near(vol, 1.0f, 0.01f), "ahead: full volume inside near radius");
  check(near(pan, 0.0f, 0.02f), "ahead: centered pan");
  check(near(pitch, 1.0f, 0.001f), "ahead: no doppler at rest");

  /* To the right of a +z-facing listener pans right (+1 side). */
  float right[3] = {10.0f, 0.0f, 0.0f};
  audio_calc_positional(listener, 0.0f, lvel, right, zero, 1.0f, &vol, &pan,
                        &pitch);
  check(pan > 0.7f, "right side pans positive");

  /* Mirrored: to the left pans left. */
  float left[3] = {-10.0f, 0.0f, 0.0f};
  audio_calc_positional(listener, 0.0f, lvel, left, zero, 1.0f, &vol, &pan,
                        &pitch);
  check(pan < -0.7f, "left side pans negative");

  /* Past the far radius is silent. */
  float far[3] = {0.0f, 0.0f, AUDIO_FAR_M + 10.0f};
  audio_calc_positional(listener, 0.0f, lvel, far, zero, 1.0f, &vol, &pan,
                        &pitch);
  check(vol <= 0.0f, "past far radius: silent");

  /* Receding source (moving away along +z) drops pitch below 1. */
  float away_vel[3] = {0.0f, 0.0f, 20.0f};
  audio_calc_positional(listener, 0.0f, lvel, ahead, away_vel, 1.0f, &vol,
                        &pan, &pitch);
  check(pitch < 1.0f, "receding source: pitch drops");

  /* Approaching source raises pitch above 1. */
  float towards_vel[3] = {0.0f, 0.0f, -20.0f};
  audio_calc_positional(listener, 0.0f, lvel, ahead, towards_vel, 1.0f, &vol,
                        &pan, &pitch);
  check(pitch > 1.0f, "approaching source: pitch rises");

  /* Listener yaw rotates the pan frame: source at +x is ahead for yaw=+90
   * (facing +x), so it centers instead of panning right. */
  audio_calc_positional(listener, (float)(3.14159265f * 0.5f), lvel, right,
                        zero, 1.0f, &vol, &pan, &pitch);
  check(near(pan, 0.0f, 0.05f), "yaw +90: +x source centers");

  /* ---- Headless lifecycle + node pool --------------------------------- */

  check(audio_init() == 0, "audio_init succeeds headless");

  AudioVoice* a = audio_play(AUDIO_SFX_UI_CONFIRM);
  check(a != NULL, "2D one-shot returns a handle");

  AudioVoice* b = audio_reserve_loop(AUDIO_SFX_ENGINE);
  check(b != NULL, "reserved loop returns a handle");

  /* The one-shot must not have consumed the loop's slot, and a second
   * one-shot must reclaim the first's slot rather than the reserved one. */
  AudioVoice* c = audio_play(AUDIO_SFX_UI_MOVE);
  check(c != NULL, "second one-shot allocates");
  check(c != b, "one-shots never steal the reserved loop");

  audio_voice_update(b, 0.5f, 0.25f, 1.5f);
  audio_voice_update(b, 2.0f, 5.0f, -1.0f); /* out-of-range: clamped, no crash */

  float pos[3] = {0.0f, 0.0f, 8.0f};
  audio_voice_set_position(b, pos, zero, 1.0f);

  AudioVoice* d = audio_play_at(AUDIO_SFX_DELIVERY, far, zero, 1.0f);
  check(d == NULL, "inaudible 3D one-shot is skipped");

  /* Buses apply live without crashing headless. */
  audio_set_sfx_volume(0.5f);
  audio_set_master_volume(0.8f);
  audio_set_music_volume(0.6f);

  /* Music controls are safe no-ops. */
  audio_music_play(AUDIO_MUSIC_GAMEPLAY, 1);
  audio_music_pause(1);
  audio_music_pause(0);
  audio_music_stop();

  /* reset frees the reserved loop; a fresh reserve works afterwards. */
  audio_reset();
  AudioVoice* e = audio_reserve_loop(AUDIO_SFX_ENGINE);
  check(e != NULL, "reserve after reset");

  audio_voice_stop(b); /* stale handle: safe no-op */
  audio_voice_stop(e);

  audio_shutdown();
  audio_shutdown(); /* idempotent */

  if (fails)
    return 1;
  printf("audio core: ok\n");
  return 0;
}
