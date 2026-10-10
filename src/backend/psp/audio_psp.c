#include "audio.h"

#include <malloc.h>
#include <pspaudio.h>
#include <pspiofilemgr.h>
#include <pspthreadman.h>
#include <psputils.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * PSP audio backend (island1-26pd): native sceAudio, no miniaudio. Ports the
 * proven Phoenix Game Engine mixer pattern (.scratch/PGE/pgeWav.c):
 *
 *  - One hardware channel via sceAudioChReserve(-1, 1024 frames, stereo) at
 *    44.1kHz. A dedicated mixer thread (prio 0x12, 64KiB stack, PGE's
 *    numbers) fills a 64-byte-aligned double buffer with a software mix,
 *    then blocks in sceAudioOutputPannedBlocking -- the hardware consume
 *    paces the thread, so the output path needs no semaphores.
 *  - SFX: resident WAV masters (16-bit or 8-bit mono), one software voice
 *    pool (32 slots, matching core's AUDIO_MAX_VOICES). Per-voice fixed
 *    point 16.16 rate conversion + linear interpolation + Q15 L/R gains,
 *    all integer math in the mixer thread. Reserved loops (engine hum) are
 *    never stolen by one-shots.
 *  - Music: WAV masters streamed from disc (UMD) into a 4096-frame stereo
 *    ring buffer, refilled between blocking outputs; 1:1 at 44.1kHz stereo.
 *    Pause = stop consuming the ring (file stays open, like wipeout's DC
 *    pause). Play/stop requests are applied by the mixer thread itself so
 *    sceIo stays single-threaded.
 *
 * Same architecture as audio_dc.c (wipeout-dc port) on Dreamcast: console
 * backends talk to the platform directly; only desktop keeps miniaudio.
 */

#define PSP_PERIOD_FRAMES 1024
#define PSP_PERIOD_BYTES (PSP_PERIOD_FRAMES * 2 * (int)sizeof(int16_t))
#define PSP_OUT_RATE 44100
#define PSP_VOICES 32
#define PSP_MUSIC_RING_FRAMES 4096 /* power of two, stereo frames */
#define PSP_PATH_MAX 512
#define PSP_Q15 32768

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
      /* Unknown chunk: skip (plus pad byte on odd sizes). */
      sceIoLseek32(fd, (int)(size + (size & 1)), PSP_SEEK_CUR);
    }
  }
}

/* ---- Path resolution (mirrors texture_gu.c roots) ---------------------- */

static int resolve_path(const char* relpath, const char* suffix, char* out,
                        int out_len) {
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

/* ---- SFX software voices ----------------------------------------------- */

typedef struct {
  volatile int active;
  int loop;
  int reserved; /* engine hum etc: never stolen */
  int16_t* data;
  uint32_t frames;
  uint32_t rate;
  volatile uint32_t pos_fx;  /* 16.16 frame position */
  volatile uint32_t step_fx; /* 16.16 frames per output sample */
  volatile int32_t gain_l, gain_r; /* Q15 */
  uint32_t seq;
} PspVoice;

static PspVoice s_voices[PSP_VOICES];
static int16_t* s_sfx_data[AUDIO_SFX_COUNT];
static uint32_t s_sfx_frames[AUDIO_SFX_COUNT];
static uint32_t s_sfx_rate[AUDIO_SFX_COUNT];
static uint32_t s_seq;

/* Volatiles the mixer thread reads; written by the game thread. */
static volatile int s_mus_paused;
static volatile int s_mus_gain_q15 = PSP_Q15;
static volatile int s_mus_req; /* 0 none, 1 play, 2 stop */
static char s_mus_path[PSP_PATH_MAX];
static volatile int s_mus_loop;

/* Mixer-thread-owned music stream state. */
typedef struct {
  int fd;
  uint32_t data_off;
  uint32_t data_len;
  uint32_t consumed; /* bytes from data start */
  int active;
  int16_t* ring; /* PSP_MUSIC_RING_FRAMES stereo frames */
  uint32_t ring_r, ring_w, ring_avail; /* frames */
} MusicState;

static MusicState s_mus;
static int s_ch = -1;
static SceUID s_mix_th = -1;
static volatile int s_quit;
static void* s_pcm_raw; /* memalign base */
static int16_t* s_pcm;  /* two aligned periods */
static void* s_ring_raw;

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

static int sfx_play(AudioSfx id, float volume, float pan, float pitch,
                    int loop) {
  if (id <= AUDIO_SFX_NONE || id >= AUDIO_SFX_COUNT || !s_sfx_data[id])
    return -1;

  /* Free slot, else steal the oldest non-reserved one-shot (reserved loops
   * are untouchable, mirroring core's pool rules). */
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
  v->active = 0; /* brief gap while rearming */
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

/* ---- Music: mixer-thread stream ---------------------------------------- */

static void music_close(void) {
  if (s_mus.fd >= 0) {
    sceIoClose(s_mus.fd);
    s_mus.fd = -1;
  }
  s_mus.active = 0;
  s_mus.consumed = 0;
  s_mus.ring_r = s_mus.ring_w = s_mus.ring_avail = 0;
}

/* Apply a pending START/STOP request. Mixer thread only. */
static void music_apply_requests(void) {
  int req = s_mus_req;
  if (req == 0)
    return;
  s_mus_req = 0;
  music_close();
  if (req != 1)
    return;

  int fd = sceIoOpen(s_mus_path, PSP_O_RDONLY, 0777);
  if (fd < 0)
    return;
  WavInfo w;
  if (wav_parse(fd, &w) != 0 || w.channels != 2 || w.bits != 16 ||
      w.rate != PSP_OUT_RATE || w.data_len == 0) {
    sceIoClose(fd);
    return;
  }
  s_mus.fd = fd;
  s_mus.data_off = w.data_off;
  s_mus.data_len = w.data_len;
  s_mus.consumed = 0;
  sceIoLseek32(fd, (int)w.data_off, PSP_SEEK_SET);
  s_mus.active = 1;
}

/* Top up the ring from disc. Mixer thread only, between blocking outputs.
 * Capped reads bound worst-case UMD latency per period; loop wraps seek to
 * the data start (an 8s master means one seek per 8s of playback). */
static void music_refill(void) {
  if (!s_mus.active || s_mus.fd < 0)
    return;
  uint32_t space = PSP_MUSIC_RING_FRAMES - s_mus.ring_avail;
  if (space > 512)
    space = 512;

  while (space > 0) {
    uint32_t left = s_mus.data_len - s_mus.consumed;
    if (left == 0) {
      if (!s_mus_loop)
        return; /* drained; mix empties the ring then goes idle */
      if (sceIoLseek32(s_mus.fd, (int)s_mus.data_off, PSP_SEEK_SET) < 0) {
        music_close();
        return;
      }
      s_mus.consumed = 0;
      continue;
    }
    uint32_t want = space * 4; /* stereo frames -> bytes */
    if (want > left)
      want = left;
    uint32_t to_end = (PSP_MUSIC_RING_FRAMES - s_mus.ring_w) * 4;
    if (want > to_end)
      want = to_end;
    int16_t* dst = s_mus.ring + ((uint64_t)s_mus.ring_w << 1);
    int n = sceIoRead(s_mus.fd, dst, (int)want);
    if (n <= 0) {
      if (!s_mus_loop)
        return;
      if (sceIoLseek32(s_mus.fd, (int)s_mus.data_off, PSP_SEEK_SET) < 0) {
        music_close();
        return;
      }
      s_mus.consumed = 0;
      continue;
    }
    uint32_t frames_read = (uint32_t)n >> 2;
    s_mus.ring_w = (s_mus.ring_w + frames_read) & (PSP_MUSIC_RING_FRAMES - 1);
    s_mus.ring_avail += frames_read;
    s_mus.consumed += (uint32_t)n;
    space -= frames_read;
  }
}

/* ---- Mixer thread ------------------------------------------------------ */

static int mix_thread(SceSize args, void* argp) {
  (void)args;
  (void)argp;
  int bufidx = 0;
  int16_t* out = s_pcm;

  while (!s_quit) {
    music_apply_requests();

    /* Integer-only mix of the voice pool + music ring into out. */
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
      if (s_mus.active && !s_mus_paused && s_mus.ring_avail > 0) {
        const int16_t* f = s_mus.ring + ((uint64_t)s_mus.ring_r << 1);
        l += (f[0] * s_mus_gain_q15) >> 15;
        r += (f[1] * s_mus_gain_q15) >> 15;
        s_mus.ring_r = (s_mus.ring_r + 1) & (PSP_MUSIC_RING_FRAMES - 1);
        s_mus.ring_avail--;
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
    sceAudioOutputPannedBlocking(s_ch, PSP_AUDIO_VOLUME_MAX,
                                 PSP_AUDIO_VOLUME_MAX, out);
    music_refill();
    bufidx ^= 1;
    out = s_pcm + (size_t)bufidx * PSP_PERIOD_FRAMES * 2;
  }
  /* Exit without self-deleting: shutdown WaitThreadEnd + DeleteThread owns
   * the thread object (ExitDeleteThread would invalidate the uid first). */
  sceKernelExitThread(0);
  return 0;
}

/* ---- Backend entry points ---------------------------------------------- */

int backend_audio_init(void) {
  memset(s_voices, 0, sizeof(s_voices));
  memset(&s_mus, 0, sizeof(s_mus));
  s_mus.fd = -1;
  s_seq = 0;
  s_quit = 0;
  s_mus_req = 0;
  s_mus_paused = 0;
  s_mus_gain_q15 = PSP_Q15;

  s_ch = sceAudioChReserve(-1, PSP_PERIOD_FRAMES, PSP_AUDIO_FORMAT_STEREO);
  if (s_ch < 0) {
    fprintf(stderr, "audio_psp: sceAudioChReserve failed 0x%x\n",
            (unsigned)s_ch);
    return -1;
  }

  /* Two periods of interleaved stereo s16, 64-byte aligned for DMA. */
  s_pcm_raw = memalign(64, (size_t)PSP_PERIOD_BYTES * 2);
  s_ring_raw = memalign(64, PSP_MUSIC_RING_FRAMES * 2 * sizeof(int16_t));
  if (!s_pcm_raw || !s_ring_raw) {
    free(s_pcm_raw);
    free(s_ring_raw);
    s_pcm_raw = s_ring_raw = NULL;
    sceAudioChRelease(s_ch);
    s_ch = -1;
    return -1;
  }
  s_pcm = (int16_t*)s_pcm_raw;
  s_mus.ring = (int16_t*)s_ring_raw;

  /* PGE numbers: prio 0x12, 64KiB stack, user thread. */
  s_mix_th = sceKernelCreateThread("island_audio", mix_thread, 0x12, 0x10000,
                                   PSP_THREAD_ATTR_USER, 0);
  if (s_mix_th < 0) {
    fprintf(stderr, "audio_psp: mixer thread failed 0x%x\n",
            (unsigned)s_mix_th);
    free(s_pcm_raw);
    free(s_ring_raw);
    s_pcm_raw = s_ring_raw = NULL;
    s_pcm = NULL;
    s_mus.ring = NULL;
    sceAudioChRelease(s_ch);
    s_ch = -1;
    return -1;
  }
  sceKernelStartThread(s_mix_th, 0, NULL);
  return 0;
}

void backend_audio_shutdown(void) {
  /* The mixer blocks in sceAudioOutputPannedBlocking for at most one period
   * (~23ms); it observes s_quit on the next loop. */
  s_quit = 1;
  if (s_mix_th >= 0) {
    sceKernelWaitThreadEnd(s_mix_th, NULL);
    sceKernelDeleteThread(s_mix_th);
    s_mix_th = -1;
  }
  music_close();
  if (s_ch >= 0) {
    sceAudioChRelease(s_ch);
    s_ch = -1;
  }
  free(s_pcm_raw);
  free(s_ring_raw);
  s_pcm_raw = s_ring_raw = NULL;
  s_pcm = NULL;
  s_mus.ring = NULL;
}

int backend_audio_sfx_load(AudioSfx id, const char* relpath) {
  char path[PSP_PATH_MAX];
  if (resolve_path(relpath, ".wav", path, sizeof(path)) != 0)
    return -1;
  int fd = sceIoOpen(path, PSP_O_RDONLY, 0777);
  if (fd < 0)
    return -1;
  WavInfo w;
  if (wav_parse(fd, &w) != 0 || w.channels != 1 ||
      (w.bits != 16 && w.bits != 8)) {
    sceIoClose(fd);
    return -1;
  }
  uint32_t frames = w.data_len / (w.bits >> 3);
  if (frames == 0 || frames > 0x100000) {
    sceIoClose(fd);
    return -1;
  }
  int16_t* buf = (int16_t*)memalign(64, frames * sizeof(int16_t));
  if (!buf) {
    sceIoClose(fd);
    return -1;
  }
  if (w.bits == 16) {
    if (sceIoRead(fd, buf, (int)(frames * 2)) != (int)(frames * 2)) {
      free(buf);
      sceIoClose(fd);
      return -1;
    }
  } else {
    unsigned char* tmp = (unsigned char*)malloc(frames);
    if (!tmp || sceIoRead(fd, tmp, (int)frames) != (int)frames) {
      free(tmp);
      free(buf);
      sceIoClose(fd);
      return -1;
    }
    for (uint32_t i = 0; i < frames; i++)
      buf[i] = (int16_t)(((int)tmp[i] - 128) << 8);
    free(tmp);
  }
  sceIoClose(fd);

  if (s_sfx_data[id])
    free(s_sfx_data[id]);
  s_sfx_data[id] = buf;
  s_sfx_rate[id] = w.rate ? w.rate : PSP_OUT_RATE;
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

int backend_audio_sfx_play(AudioSfx id, float volume, float pan, float pitch,
                           int loop) {
  return sfx_play(id, volume, pan, pitch, loop);
}

void backend_audio_voice_update(int voice, float volume, float pan,
                                float pitch) {
  voice_update(voice, volume, pan, pitch);
}

void backend_audio_voice_stop(int voice, int loop) {
  voice_stop(voice, loop);
}

int backend_audio_music_play(const char* relpath, int loop) {
  char path[PSP_PATH_MAX];
  if (resolve_path(relpath, ".wav", path, sizeof(path)) != 0)
    return -1;
  /* Hand the path to the mixer thread; wait out any in-flight request so
   * the buffer is not torn mid-apply. */
  while (s_mus_req)
    sceKernelDelayThread(100);
  snprintf(s_mus_path, sizeof(s_mus_path), "%s", path);
  s_mus_loop = loop ? 1 : 0;
  s_mus_paused = 0;
  __sync_synchronize();
  s_mus_req = 1;
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
