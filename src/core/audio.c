#include "audio.h"

#include <math.h>
#include <string.h>

/*
 * Portable audio core: registry, voice node pool, listener state, bus
 * mixing and the 3D math. Device work happens entirely behind the
 * backend_audio_* primitives (island1-850o). Everything here runs headless,
 * which is what keeps the GL-free suite green.
 */

#define AUDIO_MAX_VOICES 32

/* Node flags. */
#define AV_ACTIVE (1u << 0)
#define AV_RESERVE (1u << 1)
#define AV_LOOP (1u << 2)

struct AudioVoice {
  AudioSfx id;
  int voice; /* backend voice id */
  unsigned flags;
  unsigned seq; /* allocation age, for LRU steal when the pool is full */
  float volume, pan, pitch;
};

/* Relative paths, no extension: backends append .wav (desktop), .raw/.adpcm
 * (DC). Sourced from the audio-assets checkout (island1-zozu). */
static const char* const k_sfx_path[AUDIO_SFX_COUNT] = {
    NULL,
    "audio/sfx/ui_move",
    "audio/sfx/ui_confirm",
    "audio/sfx/ui_back",
    "audio/sfx/delivery",
    "audio/sfx/pickup",
    "audio/sfx/drop",
    "audio/sfx/engine",
};

static const char* const k_music_path[AUDIO_MUSIC_COUNT] = {
    NULL,
    "audio/music/title",
    "audio/music/gameplay",
};

static AudioVoice s_voices[AUDIO_MAX_VOICES];
static unsigned s_voice_seq;
static int s_inited = 0;

static float s_master = 1.0f;
static float s_sfx_bus = 1.0f;
static float s_music_bus = 0.7f;
static int s_music_paused = 0;

static float s_listener_pos[3];
static float s_listener_vel[3];
static float s_listener_yaw = 0.0f;

static float clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

/* ---- Positional math -------------------------------------------------- */

void audio_calc_positional(const float listener_pos[3], float listener_yaw, const float listener_vel[3], const float pos[3], const float vel[3], float volume, float* out_vol, float* out_pan, float* out_pitch) {
  float rx = pos[0] - listener_pos[0];
  float ry = pos[1] - listener_pos[1];
  float rz = pos[2] - listener_pos[2];
  float dist = sqrtf(rx * rx + ry * ry + rz * rz);

  /* Full volume inside AUDIO_NEAR_M, linear falloff to silence at
   * AUDIO_FAR_M (wipeout's 512..32768 ramp, rescaled). */
  float atten = (dist <= AUDIO_NEAR_M)
                    ? 1.0f
                    : 1.0f - (dist - AUDIO_NEAR_M) / (AUDIO_FAR_M - AUDIO_NEAR_M);
  *out_vol = clampf(atten, 0.0f, 1.0f) * volume;

  /* Pan from the horizontal bearing against the listener's right vector.
   * heading 0 faces +z; right is (cos yaw, -sin yaw) in x/z. */
  float dist_xz = sqrtf(rx * rx + rz * rz);
  if (dist_xz > 0.001f) {
    float right = (rx * cosf(listener_yaw) - rz * sinf(listener_yaw)) / dist_xz;
    *out_pan = clampf(right, -1.0f, 1.0f);
  } else {
    *out_pan = 0.0f;
  }

  /* Doppler: receding sources drop pitch (wipeout's dot-product shift,
   * rescaled for island's metre-sized world). */
  float rvx = listener_vel[0] - vel[0];
  float rvy = listener_vel[1] - vel[1];
  float rvz = listener_vel[2] - vel[2];
  float away = (dist > 0.01f) ? (rvx * rx + rvy * ry + rvz * rz) / dist : 0.0f;
  *out_pitch = 1.0f + clampf(away, -25.0f, 25.0f) * 0.01f;
}

/* ---- Node pool -------------------------------------------------------- */

static void voice_claim(AudioVoice* v, AudioSfx id, unsigned extra_flags) {
  v->id = id;
  v->flags = AV_ACTIVE | extra_flags;
  v->volume = 1.0f;
  v->pan = 0.0f;
  v->pitch = 1.0f;
  v->seq = ++s_voice_seq;
}

static AudioVoice* voice_alloc(AudioSfx id, unsigned extra_flags) {
  /* Pass 1: a truly free slot, so concurrent one-shots each get their own
   * voice instead of collapsing onto one (the old code reused any non-reserved
   * slot, orphaning the previous backend handle mid-play). */
  for (int i = 0; i < AUDIO_MAX_VOICES; i++) {
    AudioVoice* v = &s_voices[i];
    if (!(v->flags & AV_ACTIVE)) {
      voice_claim(v, id, extra_flags);
      return v;
    }
  }
  /* Pass 2: pool full. Steal the oldest non-reserved one-shot (LRU by seq);
   * never take a live reserved loop (wipeout's SFX_RESERVE rule). Stop the
   * stolen backend voice so the orphaned sound does not keep playing. */
  AudioVoice* steal = NULL;
  unsigned oldest = 0xFFFFFFFFu;
  for (int i = 0; i < AUDIO_MAX_VOICES; i++) {
    AudioVoice* v = &s_voices[i];
    if (v->flags & AV_RESERVE)
      continue;
    if (v->seq < oldest) {
      oldest = v->seq;
      steal = v;
    }
  }
  if (!steal)
    return NULL; /* only reserved loops remain */
  backend_audio_voice_stop(steal->voice, 0);
  voice_claim(steal, id, extra_flags);
  return steal;
}

static void voice_apply(AudioVoice* v) {
  backend_audio_voice_update(v->voice, clampf(v->volume * s_sfx_bus * s_master, 0.0f, 1.0f), v->pan, v->pitch);
}

/* ---- Lifecycle -------------------------------------------------------- */

int audio_init(void) {
  if (s_inited)
    return 0;
  memset(s_voices, 0, sizeof(s_voices));
  for (int i = 0; i < AUDIO_MAX_VOICES; i++)
    s_voices[i].voice = -1;
  s_master = 1.0f;
  s_sfx_bus = 1.0f;
  s_music_bus = 0.7f;
  s_music_paused = 0;
  s_listener_yaw = 0.0f;
  memset(s_listener_pos, 0, sizeof(s_listener_pos));
  memset(s_listener_vel, 0, sizeof(s_listener_vel));

  if (backend_audio_init() != 0)
    return -1;

  for (int id = 1; id < AUDIO_SFX_COUNT; id++)
    backend_audio_sfx_load((AudioSfx)id, k_sfx_path[id]);

  s_inited = 1;
  return 0;
}

void audio_shutdown(void) {
  if (!s_inited)
    return;
  audio_music_stop();
  backend_audio_sfx_unload_all();
  backend_audio_shutdown();
  s_inited = 0;
}

void audio_reset(void) {
  for (int i = 0; i < AUDIO_MAX_VOICES; i++) {
    AudioVoice* v = &s_voices[i];
    if (v->flags & AV_ACTIVE) {
      backend_audio_voice_stop(v->voice, (v->flags & AV_LOOP) ? 1 : 0);
      v->flags = 0;
      v->voice = -1;
    }
  }
}

void audio_listener(const float pos[3], float yaw, const float vel[3]) {
  s_listener_pos[0] = pos[0];
  s_listener_pos[1] = pos[1];
  s_listener_pos[2] = pos[2];
  s_listener_yaw = yaw;
  s_listener_vel[0] = vel[0];
  s_listener_vel[1] = vel[1];
  s_listener_vel[2] = vel[2];
}

/* ---- SFX -------------------------------------------------------------- */

AudioVoice* audio_play(AudioSfx id) {
  if (!s_inited || id <= AUDIO_SFX_NONE || id >= AUDIO_SFX_COUNT)
    return NULL;
  AudioVoice* v = voice_alloc(id, 0);
  if (!v)
    return NULL;
  v->voice = backend_audio_sfx_play(id, clampf(v->volume * s_sfx_bus * s_master, 0.0f, 1.0f), 0.0f, 1.0f, 0);
  if (v->voice < 0) {
    v->flags = 0;
    return NULL;
  }
  return v;
}

AudioVoice* audio_play_at(AudioSfx id, const float pos[3], const float vel[3], float volume) {
  if (!s_inited || id <= AUDIO_SFX_NONE || id >= AUDIO_SFX_COUNT)
    return NULL;
  float vol, pan, pitch;
  audio_calc_positional(s_listener_pos, s_listener_yaw, s_listener_vel, pos, vel, volume, &vol, &pan, &pitch);
  if (vol <= 0.0f)
    return NULL; /* inaudible: skip the voice entirely (wipeout behaviour) */

  AudioVoice* v = voice_alloc(id, 0);
  if (!v)
    return NULL;
  v->volume = vol;
  v->pan = pan;
  v->pitch = pitch;
  v->voice = backend_audio_sfx_play(id, clampf(vol * s_sfx_bus * s_master, 0.0f, 1.0f), pan, pitch, 0);
  if (v->voice < 0) {
    v->flags = 0;
    return NULL;
  }
  return v;
}

AudioVoice* audio_reserve_loop(AudioSfx id) {
  if (!s_inited || id <= AUDIO_SFX_NONE || id >= AUDIO_SFX_COUNT)
    return NULL;
  AudioVoice* v = voice_alloc(id, AV_RESERVE | AV_LOOP);
  if (!v)
    return NULL;
  v->volume = 0.0f;
  v->pan = 0.0f;
  v->pitch = 1.0f;
  /* Reserved loops start silent; the game ramps them up per frame. */
  v->voice = backend_audio_sfx_play(id, 0.0f, 0.0f, 1.0f, 1);
  if (v->voice < 0) {
    v->flags = 0;
    return NULL;
  }
  return v;
}

void audio_voice_update(AudioVoice* v, float volume, float pan, float pitch) {
  if (!v || !(v->flags & AV_ACTIVE))
    return;
  v->volume = clampf(volume, 0.0f, 1.0f);
  v->pan = clampf(pan, -1.0f, 1.0f);
  v->pitch = pitch > 0.0f ? pitch : 0.0f;
  voice_apply(v);
}

void audio_voice_set_position(AudioVoice* v, const float pos[3], const float vel[3], float volume) {
  if (!v || !(v->flags & AV_ACTIVE))
    return;
  float vol, pan, pitch;
  audio_calc_positional(s_listener_pos, s_listener_yaw, s_listener_vel, pos, vel, volume, &vol, &pan, &pitch);
  v->volume = vol;
  v->pan = pan;
  v->pitch = pitch;
  voice_apply(v);
}

void audio_voice_stop(AudioVoice* v) {
  if (!v || !(v->flags & AV_ACTIVE))
    return;
  backend_audio_voice_stop(v->voice, (v->flags & AV_LOOP) ? 1 : 0);
  v->flags = 0;
  v->voice = -1;
}

/* ---- Music ------------------------------------------------------------ */

void audio_music_play(AudioMusic id, int loop) {
  if (!s_inited || id <= AUDIO_MUSIC_NONE || id >= AUDIO_MUSIC_COUNT)
    return;
  s_music_paused = 0;
  backend_audio_music_play(k_music_path[id], loop);
  backend_audio_music_volume(
      clampf(s_music_bus * s_master, 0.0f, 1.0f));
}

void audio_music_stop(void) {
  if (!s_inited)
    return;
  backend_audio_music_stop();
}

void audio_music_pause(int paused) {
  if (!s_inited)
    return;
  s_music_paused = paused ? 1 : 0;
  backend_audio_music_pause(s_music_paused);
}

/* ---- Buses ------------------------------------------------------------ */

void audio_set_master_volume(float v) {
  s_master = clampf(v, 0.0f, 1.0f);
  for (int i = 0; i < AUDIO_MAX_VOICES; i++)
    if (s_voices[i].flags & AV_ACTIVE)
      voice_apply(&s_voices[i]);
  backend_audio_music_volume(clampf(s_music_bus * s_master, 0.0f, 1.0f));
}

void audio_set_sfx_volume(float v) {
  s_sfx_bus = clampf(v, 0.0f, 1.0f);
  for (int i = 0; i < AUDIO_MAX_VOICES; i++)
    if (s_voices[i].flags & AV_ACTIVE)
      voice_apply(&s_voices[i]);
}

void audio_set_music_volume(float v) {
  s_music_bus = clampf(v, 0.0f, 1.0f);
  backend_audio_music_volume(clampf(s_music_bus * s_master, 0.0f, 1.0f));
}
