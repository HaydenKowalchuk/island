#include <miniaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"

/*
 * Desktop audio backend (island1-a9pu): ma_engine with a resident SFX voice
 * pool (MA_SOUND_FLAG_DECODE) and a streaming music voice (MA_SOUND_FLAG_STREAM).
 * Volumes arrive already bus-scaled from core; pan is -1..1, pitch a rate
 * scale. ma_engine runs its own mixer thread here, so there is no per-frame
 * pump. Missing asset files are non-fatal: the game just stays silent.
 */

#define MAX_VOICES 32
#define PATH_MAX_LEN 512

typedef struct {
  ma_sound sound;
  int inited;
  int looping; /* reserved/looping voices hold their slot */
  AudioSfx id; /* which SFX this slot was decoded for */
} VoiceSlot;

static ma_engine s_engine;
static int s_engine_up = 0;
static VoiceSlot s_voices[MAX_VOICES];
static ma_sound s_music;
static int s_music_up = 0;

/* Probe the same root chain the texture loaders use, plus the dedicated
 * audio-assets checkout (island1-zozu): AUDIO_ASSET_DIR, ASSET_DIR,
 * audio_assets (repo-relative submodule), assets. */
static int resolve_path(const char* relpath, const char* suffix, char* out, int out_len) {
  static const char* roots[] = {NULL, NULL, "audio_assets", "assets"};
  roots[0] = getenv("AUDIO_ASSET_DIR");
  roots[1] = getenv("ASSET_DIR");

  for (int i = 0; i < 4; i++) {
    if (!roots[i] || !*roots[i])
      continue;
    snprintf(out, out_len, "%s/%s%s", roots[i], relpath, suffix);
    FILE* f = fopen(out, "rb");
    if (f) {
      fclose(f);
      return 0;
    }
  }
  /* Last resort: try the bare relative path from the working dir. */
  snprintf(out, out_len, "%s%s", relpath, suffix);
  FILE* f = fopen(out, "rb");
  if (f) {
    fclose(f);
    return 0;
  }
  return -1;
}

int backend_audio_init(void) {
  if (s_engine_up)
    return 0;
  memset(s_voices, 0, sizeof(s_voices));
  for (int i = 0; i < MAX_VOICES; i++)
    s_voices[i].id = AUDIO_SFX_NONE;

  ma_engine_config cfg = ma_engine_config_init();
  if (ma_engine_init(&cfg, &s_engine) != MA_SUCCESS) {
    fprintf(stderr, "audio: no device, running silent\n");
    return 0; /* headless/CI: silent, never fatal */
  }
  s_engine_up = 1;
  return 0;
}

void backend_audio_shutdown(void) {
  backend_audio_music_stop();
  backend_audio_sfx_unload_all();
  if (s_engine_up) {
    ma_engine_uninit(&s_engine);
    s_engine_up = 0;
  }
}

int backend_audio_sfx_load(AudioSfx id, const char* relpath) {
  (void)id;
  (void)relpath;
  return 0; /* decoded lazily per voice slot on first play */
}

void backend_audio_sfx_unload_all(void) {
  for (int i = 0; i < MAX_VOICES; i++) {
    if (s_voices[i].inited) {
      ma_sound_uninit(&s_voices[i].sound);
      s_voices[i].inited = 0;
      s_voices[i].id = AUDIO_SFX_NONE;
    }
  }
}

/* Relink a slot to (re)decode the given SFX; no-op if already bound. */
static int voice_bind(int voice, AudioSfx id, const char* relpath) {
  VoiceSlot* slot = &s_voices[voice];
  if (slot->inited && slot->id == id)
    return 0;

  char path[PATH_MAX_LEN];
  if (resolve_path(relpath, ".wav", path, sizeof(path)) != 0)
    return -1;

  if (slot->inited) {
    ma_sound_uninit(&slot->sound);
    slot->inited = 0;
  }
  if (ma_sound_init_from_file(&s_engine, path, MA_SOUND_FLAG_DECODE, NULL, NULL, &slot->sound) != MA_SUCCESS)
    return -1;
  slot->inited = 1;
  slot->id = id;
  return 0;
}

/* SFX relpath table, mirroring core's registry (kept in sync by hand; both
 * live with the same enum order in audio.h). */
static const char* sfx_relpath(AudioSfx id) {
  static const char* const paths[AUDIO_SFX_COUNT] = {
      NULL,
      "audio/sfx/ui_move",
      "audio/sfx/ui_confirm",
      "audio/sfx/ui_back",
      "audio/sfx/delivery",
      "audio/sfx/pickup",
      "audio/sfx/drop",
      "audio/sfx/engine",
  };
  return (id > AUDIO_SFX_NONE && id < AUDIO_SFX_COUNT) ? paths[id] : NULL;
}

/* Pick a pool slot: free slots first, then one-shots that have finished, then
 * (for one-shots only) the first non-looping slot. Loops never steal. */
static int voice_pick(int loop) {
  int steal = -1;
  for (int i = 0; i < MAX_VOICES; i++) {
    VoiceSlot* slot = &s_voices[i];
    if (!slot->inited)
      return i;
    if (slot->looping)
      continue;
    ma_uint64 len = 0, cur = 0;
    ma_sound_get_length_in_pcm_frames(&slot->sound, &len);
    ma_sound_get_cursor_in_pcm_frames(&slot->sound, &cur);
    if (len > 0 && cur >= len)
      return i; /* finished one-shot: free to reuse */
    if (steal < 0)
      steal = i;
  }
  if (!loop && steal >= 0)
    return steal;
  return -1;
}

int backend_audio_sfx_play(AudioSfx id, float volume, float pan, float pitch, int loop) {
  if (!s_engine_up)
    return -1;
  const char* relpath = sfx_relpath(id);
  if (!relpath)
    return -1;

  int voice = voice_pick(loop);
  if (voice < 0 || voice_bind(voice, id, relpath) != 0)
    return -1;

  ma_sound* s = &s_voices[voice].sound;
  s_voices[voice].looping = loop ? 1 : 0;
  ma_sound_seek_to_pcm_frame(s, 0);
  ma_sound_set_volume(s, volume);
  ma_sound_set_pan(s, pan);
  ma_sound_set_pitch(s, pitch > 0.0f ? pitch : 1.0f);
  ma_sound_set_looping(s, loop ? MA_TRUE : MA_FALSE);
  ma_sound_start(s);
  return voice;
}

void backend_audio_voice_update(int voice, float volume, float pan, float pitch) {
  if (!s_engine_up || voice < 0 || voice >= MAX_VOICES ||
      !s_voices[voice].inited)
    return;
  ma_sound* s = &s_voices[voice].sound;
  ma_sound_set_volume(s, volume);
  ma_sound_set_pan(s, pan);
  ma_sound_set_pitch(s, pitch > 0.0f ? pitch : 1.0f);
}

void backend_audio_voice_stop(int voice, int loop) {
  (void)loop;
  if (!s_engine_up || voice < 0 || voice >= MAX_VOICES ||
      !s_voices[voice].inited)
    return;
  ma_sound_stop(&s_voices[voice].sound);
  s_voices[voice].looping = 0;
}

int backend_audio_music_play(const char* relpath, int loop) {
  if (!s_engine_up)
    return -1;

  char path[PATH_MAX_LEN];
  if (resolve_path(relpath, ".wav", path, sizeof(path)) != 0)
    return -1;

  if (s_music_up) {
    ma_sound_stop(&s_music);
    ma_sound_uninit(&s_music);
    s_music_up = 0;
  }
  if (ma_sound_init_from_file(&s_engine, path, MA_SOUND_FLAG_STREAM, NULL, NULL, &s_music) != MA_SUCCESS)
    return -1;
  s_music_up = 1;
  ma_sound_set_looping(&s_music, loop ? MA_TRUE : MA_FALSE);
  ma_sound_start(&s_music);
  return 0;
}

void backend_audio_music_stop(void) {
  if (!s_music_up)
    return;
  ma_sound_stop(&s_music);
  ma_sound_uninit(&s_music);
  s_music_up = 0;
}

void backend_audio_music_pause(int paused) {
  if (!s_music_up)
    return;
  if (paused)
    ma_sound_stop(&s_music);
  else
    ma_sound_start(&s_music);
}

void backend_audio_music_volume(float v) {
  if (!s_music_up)
    return;
  ma_sound_set_volume(&s_music, v);
}
