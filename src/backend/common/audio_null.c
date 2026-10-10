#include "audio.h"

/*
 * Null audio backend: every primitive succeeds (or returns a harmless dummy
 * voice) without touching a device. Linked into the GL-free test suite and
 * into the PSP build until island1-26pd decides that platform's strategy, so
 * core game code can call audio_* unconditionally (island1-850o).
 */

int backend_audio_init(void) {
  return 0;
}

void backend_audio_shutdown(void) {
}

int backend_audio_sfx_load(AudioSfx id, const char* relpath) {
  (void)id;
  (void)relpath;
  return 0;
}

void backend_audio_sfx_unload_all(void) {
}

int backend_audio_sfx_play(AudioSfx id, float volume, float pan, float pitch,
                           int loop) {
  (void)id;
  (void)volume;
  (void)pan;
  (void)pitch;
  (void)loop;
  return 0; /* dummy voice id; updates/stops land harmlessly */
}

void backend_audio_voice_update(int voice, float volume, float pan,
                                float pitch) {
  (void)voice;
  (void)volume;
  (void)pan;
  (void)pitch;
}

void backend_audio_voice_stop(int voice, int loop) {
  (void)voice;
  (void)loop;
}

int backend_audio_music_play(const char* relpath, int loop) {
  (void)relpath;
  (void)loop;
  return 0;
}

void backend_audio_music_stop(void) {
}

void backend_audio_music_pause(int paused) {
  (void)paused;
}

void backend_audio_music_volume(float v) {
  (void)v;
}
