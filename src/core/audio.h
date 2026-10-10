#ifndef ISLAND_CORE_AUDIO_H
#define ISLAND_CORE_AUDIO_H

/*
 * Platform-agnostic audio contract (island1-850o), shaped after wipeout-dc's
 * sfx module: SFX handles, 2D/3D one-shots, reserved looping voices with
 * per-frame updates, single-track music, volume buses. Core owns the node
 * pool and the 3D math (portable, GL-free testable); each backend implements
 * the backend_audio_* primitives at the bottom of this file with its native
 * device (desktop: miniaudio; Dreamcast: KOS snd_stream + sfxmgr; PSP: native
 * sceAudio software mixer, PGE-style). No platform types leak into this
 * header.
 */

typedef enum {
  AUDIO_SFX_NONE = 0,
  AUDIO_SFX_UI_MOVE,
  AUDIO_SFX_UI_CONFIRM,
  AUDIO_SFX_UI_BACK,
  AUDIO_SFX_DELIVERY,
  AUDIO_SFX_PICKUP,
  AUDIO_SFX_DROP,
  AUDIO_SFX_ENGINE,
  AUDIO_SFX_COUNT
} AudioSfx;

typedef enum {
  AUDIO_MUSIC_NONE = 0,
  AUDIO_MUSIC_TITLE,
  AUDIO_MUSIC_GAMEPLAY,
  AUDIO_MUSIC_COUNT
} AudioMusic;

/* Opaque live-voice handle; valid until stop/reset, NULL if pool exhausted. */
typedef struct AudioVoice AudioVoice;

/* Lifecycle. init returns 0 on success or headless no-op; never aborts. */
int audio_init(void);
void audio_shutdown(void);

/* Scene-change teardown: stop one-shots and free reserved loops. */
void audio_reset(void);

/* 2D one-shot at full volume, centered. Returns a handle for tweaks (may be
 * NULL when the pool is exhausted or headless). */
AudioVoice* audio_play(AudioSfx id);

/* 3D one-shot: world position, source velocity (for doppler), volume 0..1.
 * Falloff/pan/pitch are computed from the listener set by audio_listener. */
AudioVoice* audio_play_at(AudioSfx id, const float pos[3], const float vel[3], float volume);

/* Dedicated looping voice (engine hum, ambience): reserved so one-shots never
 * steal its channel; starts silent (volume 0). */
AudioVoice* audio_reserve_loop(AudioSfx id);

/* Per-frame push of a live voice's mix params (wipeout sfx_update_ex shape). */
void audio_voice_update(AudioVoice* v, float volume, float pan, float pitch);

/* Re-derive volume/pan/pitch from a world position and push (positional
 * loops: crowd, remote engines). */
void audio_voice_set_position(AudioVoice* v, const float pos[3], const float vel[3], float volume);

void audio_voice_stop(AudioVoice* v);

/* Listener for 3D sounds: ear position, yaw (heading, 0 = facing +z), and
 * optional velocity for doppler (pass zero to disable). */
void audio_listener(const float pos[3], float yaw, const float vel[3]);

/* Music: single track, start/replace, optional loop. pause keeps the stream
 * alive silently on DC (wipeout trick). */
void audio_music_play(AudioMusic id, int loop);
void audio_music_stop(void);
void audio_music_pause(int paused);

/* Buses, 0..1. Applied to live voices and the current stream on change. */
void audio_set_master_volume(float v);
void audio_set_sfx_volume(float v);
void audio_set_music_volume(float v);

/*
 * Positional math, exposed for the GL-free unit test (island1-850o). Near/
 * far are metres in island's world scale (wipeout's 512..32768 units do not
 * apply here). pan is -1 hard left .. +1 hard right; pitch is a playback
 * rate scale around 1.0.
 */
#define AUDIO_NEAR_M 4.0f
#define AUDIO_FAR_M 90.0f
void audio_calc_positional(const float listener_pos[3], float listener_yaw, const float listener_vel[3], const float pos[3], const float vel[3], float volume, float* out_vol, float* out_pan, float* out_pitch);

/* ---- Backend primitives ------------------------------------------------
 * Implemented per platform (audio_desktop.c / audio_dc.c / audio_psp.c) or
 * as no-ops in backend/common/audio_null.c (GL-free tests). Volumes
 * arriving here are already scaled by the buses. */

int backend_audio_init(void);
void backend_audio_shutdown(void);

/* Load one SFX id from `relpath` (no extension; backend resolves root and
 * suffix). Return 0 on success; missing files are non-fatal (stay silent). */
int backend_audio_sfx_load(AudioSfx id, const char* relpath);
void backend_audio_sfx_unload_all(void);

/* Play: loop!=0 starts a looping voice at volume 0 (reserve semantics).
 * Returns a backend voice id (>=0) or -1. */
int backend_audio_sfx_play(AudioSfx id, float volume, float pan, float pitch, int loop);
void backend_audio_voice_update(int voice, float volume, float pan, float pitch);
void backend_audio_voice_stop(int voice, int loop);

int backend_audio_music_play(const char* relpath, int loop);
void backend_audio_music_stop(void);
void backend_audio_music_pause(int paused);
void backend_audio_music_volume(float v);

#endif
