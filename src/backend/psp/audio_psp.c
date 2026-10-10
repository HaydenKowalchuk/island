#include <malloc.h>
#include <pspaudio.h>
#include <pspiofilemgr.h>
#include <pspthreadman.h>
#include <psputils.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"

/*
 * PSP audio backend (island1-26pd): native sceAudio, no miniaudio. Ports the
 * proven Phoenix Game Engine mixer pattern (.scratch/PGE/pgeWav.c).
 *
 *  - One hardware channel via sceAudioChReserve(-1, 1024 frames, stereo) at
 *    44.1kHz. A dedicated mixer thread (prio 0x12, 64KiB stack, PGE's
 *    numbers) fills a 64-byte-aligned double buffer with a software mix,
 *    then blocks in sceAudioOutputPannedBlocking -- the hardware consume
 *    paces the thread, so the output path needs no semaphores.
 *  - EVERYTHING IS RESIDENT. UMD storage is far too slow to touch from the
 *    mixer thread: streaming music from disc caused both dropouts (refill
 *    could not keep up with the 1024-frame period) and frame hitches (a
 *    high-priority thread blocking on sceIoRead starved the game loop).
 *    SFX and music are both fully loaded into RAM (PSP has 64MB; our tracks
 *    are ~1.4MB each). The mixer thread never does file IO.
 *  - SFX: resident WAV (16/8-bit mono), 32 software voices (matching core's
 *    AUDIO_MAX_VOICES). Per-voice 16.16 fixed-point resample + linear
 *    interp + Q15 L/R gains, all integer math. Reserved loops (engine hum)
 *    never stolen by one-shots.
 *  - Music: resident WAV (16-bit stereo 44.1kHz), one track at a time, loop
 *    wrap in the mixer. Loaded on first play and cached per path (a one-time
 *    UMD read on the game thread during a transition), so switching between
 *    the 2 tracks is a pointer swap after the first play of each.
 *
 * Same architecture as audio_dc.c (wipeout-dc port) on Dreamcast: console
 * backends talk to the platform directly; only desktop keeps miniaudio.
 */

#define PSP_PERIOD_FRAMES 1024
#define PSP_PERIOD_BYTES (PSP_PERIOD_FRAMES * 2 * (int)sizeof(int16_t))
#define PSP_OUT_RATE 44100
#define PSP_VOICES 32
#define PSP_PATH_MAX 512
#define PSP_Q15 32768
#define PSP_MUSIC_CACHE 4

/* ---- WAV parsing ------------------------------------------------------- */

typedef struct {
  uint32_t rate;
  uint16_t channels;
  uint16_t bits;
  uint32_t data_off;
  uint32_t data_len;
} WavInfo;

/* Walk RIFF chunks; 0 = ok. Masters are canonical PCM but we walk anyway. */
static int wav_parse(int fd, WavInfo* w) {
  memset(w, 0, sizeof(*w));
  unsigned char hdr[12];
  if (sceIoRead(fd, hdr, 12) != 12)
    return -1;
  if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0)
    return -1;

  for (;;) {
    unsigned char ch[8];
    if (sceIoRead(fd, ch, 8) != 8)
      return -1;
    uint32_t size = (uint32_t)ch[4] | ((uint32_t)ch[5] << 8) |
                    ((uint32_t)ch[6] << 16) | ((uint32_t)ch[7] << 24);
    if (memcmp(ch, "fmt ", 4) == 0) {
      unsigned char fmt[16];
      if (size < 16 || sceIoRead(fd, fmt, 16) != 16)
        return -1;
      uint16_t codec = (uint16_t)(fmt[0] | (fmt[1] << 8));
      if (codec != 1)
        return -1; /* PCM only */
      w->channels = (uint16_t)(fmt[2] | (fmt[3] << 8));
      w->rate = (uint32_t)fmt[4] | ((uint32_t)fmt[5] << 8) |
                ((uint32_t)fmt[6] << 16) | ((uint32_t)fmt[7] << 24);
      w->bits = (uint16_t)(fmt[14] | (fmt[15] << 8));
      if (size > 16)
        sceIoLseek32(fd, (int)(size - 16), PSP_SEEK_CUR);
    } else if (memcmp(ch, "data", 4) == 0) {
      w->data_off = (uint32_t)sceIoLseek32(fd, 0, PSP_SEEK_CUR);
      w->data_len = size;
      return 0;
    } else {
      sceIoLseek32(fd, (int)(size + (size & 1)), PSP_SEEK_CUR);
    }
  }
}

/* ---- Path resolution (mirrors texture_gu.c roots) ---------------------- */

static int resolve_path(const char* relpath, const char* suffix, char* out, int out_len) {
  static const char* roots[5];
  roots[0] = getenv("AUDIO_ASSET_DIR");
  roots[1] = getenv("ASSET_DIR");
  roots[2] = "disc0:/PSP_GAME/USRDIR/assets";
  roots[3] = "host0:/assets";
  roots[4] = "ms0:/PSP/GAME/island/assets";

  for (int i = 0; i < 5; i++) {
    if (!roots[i] || !*roots[i])
      continue;
    snprintf(out, out_len, "%s/%s%s", roots[i], relpath, suffix);
    int fd = sceIoOpen(out, PSP_O_RDONLY, 0777);
    if (fd >= 0) {
      sceIoClose(fd);
      return 0;
    }
  }
  return -1;
}

/* Load an entire WAV's data chunk into a fresh buffer (mono or stereo). On
 * success sets *out_data (caller frees), *out_frames, *out_rate, *out_ch.
 * 8-bit mono is expanded to 16-bit here so the mixer stays 16-bit only. */
static int wav_load_resident(const char* path, int expect_ch, int16_t** out_data, uint32_t* out_frames, uint32_t* out_rate, uint16_t* out_ch) {
  int fd = sceIoOpen(path, PSP_O_RDONLY, 0777);
  if (fd < 0)
    return -1;
  WavInfo w;
  if (wav_parse(fd, &w) != 0 || (expect_ch && w.channels != expect_ch) ||
      (w.bits != 16 && w.bits != 8) || w.data_len == 0) {
    sceIoClose(fd);
    return -1;
  }
  uint32_t bytes_per_frame = (uint32_t)w.channels * (w.bits >> 3);
  uint32_t frames = w.data_len / bytes_per_frame;
  if (frames == 0 || frames > 0x400000) {
    sceIoClose(fd);
    return -1;
  }
  int16_t* buf = (int16_t*)memalign(64, frames * w.channels * sizeof(int16_t));
  if (!buf) {
    sceIoClose(fd);
    return -1;
  }
  if (w.bits == 16) {
    if (sceIoRead(fd, buf, (int)(frames * bytes_per_frame)) !=
        (int)(frames * bytes_per_frame)) {
      free(buf);
      sceIoClose(fd);
      return -1;
    }
  } else {
    uint32_t samples = frames * w.channels;
    unsigned char* tmp = (unsigned char*)malloc(samples);
    if (!tmp || sceIoRead(fd, tmp, (int)samples) != (int)samples) {
      free(tmp);
      free(buf);
      sceIoClose(fd);
      return -1;
    }
    for (uint32_t i = 0; i < samples; i++)
      buf[i] = (int16_t)(((int)tmp[i] - 128) << 8);
    free(tmp);
  }
  sceIoClose(fd);
  *out_data = buf;
  *out_frames = frames;
  *out_rate = w.rate ? w.rate : PSP_OUT_RATE;
  *out_ch = w.channels;
  return 0;
}

/* ---- SFX software voices ----------------------------------------------- */

typedef struct {
  volatile int active;
  int loop;
  int reserved; /* engine hum etc: never stolen */
  int16_t* data;
  uint32_t frames;
  uint32_t rate;
  volatile uint32_t pos_fx;        /* 16.16 frame position */
  volatile uint32_t step_fx;       /* 16.16 frames per output sample */
  volatile int32_t gain_l, gain_r; /* Q15 */
  uint32_t seq;
} PspVoice;

static PspVoice s_voices[PSP_VOICES];
static int16_t* s_sfx_data[AUDIO_SFX_COUNT];
static uint32_t s_sfx_frames[AUDIO_SFX_COUNT];
static uint32_t s_sfx_rate[AUDIO_SFX_COUNT];
static uint32_t s_seq;

/* Music: resident track cache + the one currently playing. The game thread
 * loads/switches; the mixer thread only ever reads s_mus (pointer swap). */
typedef struct {
  char path[PSP_PATH_MAX];
  int16_t* data; /* stereo interleaved */
  uint32_t frames;
} MusicCache;

typedef struct {
  volatile const int16_t* data;
  volatile uint32_t frames;
  volatile uint32_t pos; /* stereo frame index */
  volatile int active;
  volatile int loop;
} MusicVoice;

static MusicCache s_mus_cache[PSP_MUSIC_CACHE];
static MusicVoice s_mus;
static volatile int s_mus_paused;
static volatile int s_mus_gain_q15 = PSP_Q15;
static volatile int s_mus_req; /* 0 none, 1 play, 2 stop */
static volatile int s_mus_req_loop;

static int s_ch = -1;
static SceUID s_mix_th = -1;
static volatile int s_quit;
static void* s_pcm_raw;
static int16_t* s_pcm;

static uint32_t voice_step(uint32_t rate, float pitch) {
  if (pitch <= 0.0f)
    pitch = 1.0f;
  if (pitch > 4.0f)
    pitch = 4.0f;
  uint32_t pitch_fx = (uint32_t)(pitch * 65536.0f);
  return (uint32_t)(((uint64_t)rate * pitch_fx) / PSP_OUT_RATE);
}

static void voice_gains(PspVoice* v, float volume, float pan) {
  if (volume < 0.0f)
    volume = 0.0f;
  if (volume > 1.0f)
    volume = 1.0f;
  if (pan < -1.0f)
    pan = -1.0f;
  if (pan > 1.0f)
    pan = 1.0f;
  float l = 1.0f - (pan > 0.0f ? pan : 0.0f);
  float r = 1.0f + (pan < 0.0f ? pan : 0.0f);
  v->gain_l = (int32_t)(l * volume * PSP_Q15);
  v->gain_r = (int32_t)(r * volume * PSP_Q15);
}

static int sfx_play(AudioSfx id, float volume, float pan, float pitch, int loop) {
  if (id <= AUDIO_SFX_NONE || id >= AUDIO_SFX_COUNT || !s_sfx_data[id])
    return -1;

  int slot = -1;
  for (int i = 0; i < PSP_VOICES; i++) {
    if (!s_voices[i].active) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    uint32_t oldest = 0xFFFFFFFFu;
    for (int i = 0; i < PSP_VOICES; i++) {
      if (!s_voices[i].reserved && s_voices[i].seq < oldest) {
        oldest = s_voices[i].seq;
        slot = i;
      }
    }
    if (slot < 0)
      return -1;
  }

  PspVoice* v = &s_voices[slot];
  v->active = 0;
  v->loop = loop ? 1 : 0;
  v->reserved = loop ? 1 : 0;
  v->data = s_sfx_data[id];
  v->frames = s_sfx_frames[id];
  v->rate = s_sfx_rate[id];
  v->pos_fx = 0;
  v->step_fx = voice_step(v->rate, pitch);
  voice_gains(v, volume, pan);
  v->seq = ++s_seq;
  __sync_synchronize();
  v->active = 1;
  return slot;
}

static void voice_update(int voice, float volume, float pan, float pitch) {
  if (voice < 0 || voice >= PSP_VOICES || !s_voices[voice].active)
    return;
  PspVoice* v = &s_voices[voice];
  voice_gains(v, volume, pan);
  v->step_fx = voice_step(v->rate, pitch);
}

static void voice_stop(int voice, int loop) {
  (void)loop;
  if (voice < 0 || voice >= PSP_VOICES)
    return;
  s_voices[voice].active = 0;
}

/* ---- Mixer thread ------------------------------------------------------ */

static int mix_thread(SceSize args, void* argp) {
  (void)args;
  (void)argp;
  int bufidx = 0;
  int16_t* out = s_pcm;

  while (!s_quit) {
    /* Apply a pending music pointer swap (cheap; no IO). */
    int req = s_mus_req;
    if (req) {
      s_mus_req = 0;
      if (req == 2) {
        s_mus.active = 0;
        s_mus.data = NULL;
        s_mus.frames = 0;
        s_mus.pos = 0;
      }
      /* req == 1: the game thread already loaded the track into the cache
       * and staged s_mus.data/frames/loop; we just turn it on at pos 0. */
    }

    for (int i = 0; i < PSP_PERIOD_FRAMES; i++) {
      int32_t l = 0, r = 0;
      for (int vi = 0; vi < PSP_VOICES; vi++) {
        PspVoice* v = &s_voices[vi];
        if (!v->active)
          continue;
        uint32_t pos = v->pos_fx;
        uint32_t idx = pos >> 16;
        if (idx >= v->frames) {
          if (v->loop) {
            pos -= v->frames << 16;
            idx = pos >> 16;
            if (idx >= v->frames) {
              v->active = 0;
              continue;
            }
          } else {
            v->active = 0;
            continue;
          }
        }
        uint32_t i1 = idx + 1;
        if (i1 >= v->frames)
          i1 = v->loop ? 0 : idx;
        int32_t s0 = v->data[idx];
        int32_t s1 = v->data[i1];
        int32_t frac = (int32_t)(pos & 0xFFFF);
        int32_t s = s0 + (((s1 - s0) * frac) >> 15);
        l += (s * v->gain_l) >> 15;
        r += (s * v->gain_r) >> 15;
        v->pos_fx = pos + v->step_fx;
      }
      if (s_mus.active && !s_mus_paused) {
        const volatile int16_t* md = s_mus.data;
        uint32_t mf = s_mus.frames;
        uint32_t mpos = s_mus.pos;
        if (md && mf) {
          const volatile int16_t* f = md + ((uint64_t)mpos << 1);
          l += (f[0] * s_mus_gain_q15) >> 15;
          r += (f[1] * s_mus_gain_q15) >> 15;
          mpos++;
          if (mpos >= mf) {
            if (s_mus.loop) {
              mpos = 0;
            } else {
              s_mus.active = 0;
              mpos = 0;
            }
          }
          s_mus.pos = mpos;
        }
      }
      if (l > 32767)
        l = 32767;
      else if (l < -32768)
        l = -32768;
      if (r > 32767)
        r = 32767;
      else if (r < -32768)
        r = -32768;
      out[i * 2] = (int16_t)l;
      out[i * 2 + 1] = (int16_t)r;
    }

    sceKernelDcacheWritebackRange(out, PSP_PERIOD_BYTES);
    sceAudioOutputPannedBlocking(s_ch, PSP_AUDIO_VOLUME_MAX, PSP_AUDIO_VOLUME_MAX, out);
    bufidx ^= 1;
    out = s_pcm + (size_t)bufidx * PSP_PERIOD_FRAMES * 2;
  }
  sceKernelExitThread(0);
  return 0;
}

/* ---- Backend entry points ---------------------------------------------- */

int backend_audio_init(void) {
  memset(s_voices, 0, sizeof(s_voices));
  memset(s_mus_cache, 0, sizeof(s_mus_cache));
  memset((void*)&s_mus, 0, sizeof(s_mus));
  s_seq = 0;
  s_quit = 0;
  s_mus_req = 0;
  s_mus_paused = 0;
  s_mus_gain_q15 = PSP_Q15;

  s_ch = sceAudioChReserve(-1, PSP_PERIOD_FRAMES, PSP_AUDIO_FORMAT_STEREO);
  if (s_ch < 0) {
    fprintf(stderr, "audio_psp: sceAudioChReserve failed 0x%x\n", (unsigned)s_ch);
    return -1;
  }

  s_pcm_raw = memalign(64, (size_t)PSP_PERIOD_BYTES * 2);
  if (!s_pcm_raw) {
    sceAudioChRelease(s_ch);
    s_ch = -1;
    return -1;
  }
  s_pcm = (int16_t*)s_pcm_raw;

  s_mix_th = sceKernelCreateThread("island_audio", mix_thread, 0x12, 0x10000, PSP_THREAD_ATTR_USER, 0);
  if (s_mix_th < 0) {
    fprintf(stderr, "audio_psp: mixer thread failed 0x%x\n", (unsigned)s_mix_th);
    free(s_pcm_raw);
    s_pcm_raw = NULL;
    s_pcm = NULL;
    sceAudioChRelease(s_ch);
    s_ch = -1;
    return -1;
  }
  sceKernelStartThread(s_mix_th, 0, NULL);
  return 0;
}

void backend_audio_shutdown(void) {
  s_quit = 1;
  if (s_mix_th >= 0) {
    sceKernelWaitThreadEnd(s_mix_th, NULL);
    sceKernelDeleteThread(s_mix_th);
    s_mix_th = -1;
  }
  s_mus.active = 0;
  if (s_ch >= 0) {
    sceAudioChRelease(s_ch);
    s_ch = -1;
  }
  free(s_pcm_raw);
  s_pcm_raw = NULL;
  s_pcm = NULL;
  for (int i = 0; i < PSP_MUSIC_CACHE; i++) {
    free(s_mus_cache[i].data);
    s_mus_cache[i].data = NULL;
  }
}

int backend_audio_sfx_load(AudioSfx id, const char* relpath) {
  char path[PSP_PATH_MAX];
  if (resolve_path(relpath, ".wav", path, sizeof(path)) != 0)
    return -1;
  int16_t* data;
  uint32_t frames, rate;
  uint16_t ch;
  if (wav_load_resident(path, 1, &data, &frames, &rate, &ch) != 0)
    return -1;
  if (s_sfx_data[id])
    free(s_sfx_data[id]);
  s_sfx_data[id] = data;
  s_sfx_rate[id] = rate;
  s_sfx_frames[id] = frames;
  return 0;
}

void backend_audio_sfx_unload_all(void) {
  for (int i = 0; i < PSP_VOICES; i++)
    s_voices[i].active = 0;
  __sync_synchronize();
  for (int i = 0; i < AUDIO_SFX_COUNT; i++) {
    if (s_sfx_data[i]) {
      free(s_sfx_data[i]);
      s_sfx_data[i] = NULL;
    }
    s_sfx_frames[i] = 0;
  }
}

int backend_audio_sfx_play(AudioSfx id, float volume, float pan, float pitch, int loop) {
  return sfx_play(id, volume, pan, pitch, loop);
}

void backend_audio_voice_update(int voice, float volume, float pan, float pitch) {
  voice_update(voice, volume, pan, pitch);
}

void backend_audio_voice_stop(int voice, int loop) {
  voice_stop(voice, loop);
}

/* Find (or load into) the resident cache for this path. Game thread. */
static MusicCache* music_cache_get(const char* path) {
  for (int i = 0; i < PSP_MUSIC_CACHE; i++) {
    if (s_mus_cache[i].data && strcmp(s_mus_cache[i].path, path) == 0)
      return &s_mus_cache[i];
  }
  for (int i = 0; i < PSP_MUSIC_CACHE; i++) {
    if (!s_mus_cache[i].data) {
      int16_t* data;
      uint32_t frames, rate;
      uint16_t ch;
      if (wav_load_resident(path, 2, &data, &frames, &rate, &ch) != 0)
        return NULL;
      snprintf(s_mus_cache[i].path, sizeof(s_mus_cache[i].path), "%s", path);
      s_mus_cache[i].data = data;
      s_mus_cache[i].frames = frames;
      return &s_mus_cache[i];
    }
  }
  /* Cache full: evict slot 0 (only 2 tracks in practice). */
  free(s_mus_cache[0].data);
  s_mus_cache[0].data = NULL;
  return music_cache_get(path);
}

int backend_audio_music_play(const char* relpath, int loop) {
  char path[PSP_PATH_MAX];
  if (resolve_path(relpath, ".wav", path, sizeof(path)) != 0)
    return -1;
  /* Load-to-RAM on first play (one UMD read on the game thread, masked by
   * the scene transition); cached thereafter so track switches are instant. */
  MusicCache* c = music_cache_get(path);
  if (!c)
    return -1;

  while (s_mus_req)
    sceKernelDelayThread(100);
  /* Stage the new track; the mixer turns it on at the next period. */
  s_mus.data = c->data;
  s_mus.frames = c->frames;
  s_mus.pos = 0;
  s_mus.loop = loop ? 1 : 0;
  s_mus_paused = 0;
  __sync_synchronize();
  s_mus_req = 1;
  s_mus.active = 1;
  return 0;
}

void backend_audio_music_stop(void) {
  while (s_mus_req)
    sceKernelDelayThread(100);
  __sync_synchronize();
  s_mus_req = 2;
}

void backend_audio_music_pause(int paused) {
  s_mus_paused = paused ? 1 : 0;
}

void backend_audio_music_volume(float v) {
  if (v < 0.0f)
    v = 0.0f;
  if (v > 1.0f)
    v = 1.0f;
  s_mus_gain_q15 = (int)(v * PSP_Q15);
}
