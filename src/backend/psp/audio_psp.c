#include <malloc.h>
#include <pspaudio.h>
#include <pspiofilemgr.h>
#include <pspmp3.h>
#include <pspthreadman.h>
#include <psputility.h>
#include <psputils.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "psp_assets.h"

/*
 * PSP audio backend (island1-26pd / island1-ym91): native sceAudio mixer +
 * sceMp3 music streaming, no miniaudio. Ports the proven Phoenix Game Engine
 * mixer pattern (.scratch/PGE/pgeWav.c) and the SDK mp3 sample's decode loop.
 *
 *  - One hardware channel via sceAudioChReserve(-1, 1024 frames, stereo) at
 *    44.1kHz. A dedicated mixer thread (prio 0x12, 64KiB stack, PGE's
 *    numbers) fills a 64-byte-aligned double buffer with a software mix,
 *    then blocks in sceAudioOutputPannedBlocking -- the hardware consume
 *    paces the thread, so the output path needs no semaphores.
 *  - SFX: resident WAV (16/8-bit mono), 32 software voices (matching core's
 *    AUDIO_MAX_VOICES). Per-voice 16.16 fixed-point resample + linear
 *    interp + Q15 L/R gains, all integer math. Reserved loops (engine hum)
 *    never stolen by one-shots. Fully in RAM (tens of KB each) -- cheap.
 *  - MUSIC: streamed 128kbps MP3 via sceMp3, decoded on the Media Engine
 *    (near-zero Allegrex cost). A 1-minute track is ~1MB, not ~10MB as WAV.
 *    A dedicated music thread (lower prio than the mixer) owns the sceMp3
 *    handle: it feeds the compressed stream from disc (~16KB/s -- trivial for
 *    UMD) and copies decoded stereo 44.1kHz PCM into a ring buffer. The mixer
 *    only pops PCM from the ring; it never does IO or sceMp3 calls, so the
 *    UMD-stall-that-starved-the-frame-loop bug cannot recur.
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

/* Music ring buffer: power-of-two stereo frames (~0.74s @44.1k) sized to
 * absorb UMD latency spikes so the mixer never underruns. */
#define PSP_RING_FRAMES 32768
#define PSP_RING_MASK (PSP_RING_FRAMES - 1)
#define PSP_MUSIC_PRIO 0x1A /* below mixer 0x12: mixing preempts decode/IO */
#define MP3_STREAM_BUF (32 * 1024)   /* compressed ring in sceMp3 (>=8192) */
#define MP3_PCM_BUF (8 * 1152 * 2 * 2) /* decode scratch: several MP3 frames */

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

/* ---- Path resolution (shared with texture_gu via psp_assets.c) ---------- */

/* Build "<relpath><suffix>" and resolve it through the boot-kind-aware
 * resolver. Returns 0 with the full path in out, or -1 if not found anywhere.
 * No getenv: PSP firmware has no environment; the boot kind orders the roots. */
static int resolve_path(const char* relpath, const char* suffix, char* out, int out_len) {
  char rel[PSP_PATH_MAX];
  snprintf(rel, sizeof(rel), "%s%s", relpath, suffix);
  return psp_asset_path(rel, out, out_len) ? 0 : -1;
}

/* Load an entire WAV's data chunk into a fresh MONO 16-bit buffer. On success
 * sets *out_data (caller frees), *out_frames, *out_rate, *out_ch (=1). Accepts
 * mono or stereo masters; stereo is downmixed in place so the mixer's hot loop
 * stays single-sample-per-frame (the asset pipeline already emits mono, so this
 * stereo path is a defensive fallback, not the norm). 8-bit is expanded to
 * 16-bit here so the mixer stays 16-bit only. */
static int wav_load_resident(const char* path, int16_t** out_data, uint32_t* out_frames, uint32_t* out_rate, uint16_t* out_ch) {
  int fd = sceIoOpen(path, PSP_O_RDONLY, 0777);
  if (fd < 0)
    return -1;
  WavInfo w;
  if (wav_parse(fd, &w) != 0 || (w.channels != 1 && w.channels != 2) ||
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
  /* Stereo -> mono downmix, in place. Write index i never clobbers an unread
   * sample: future reads are at 2j,2j+1 for j>i, and i < 2j always. */
  if (w.channels == 2) {
    for (uint32_t i = 0; i < frames; i++)
      buf[i] = (int16_t)(((int32_t)buf[i * 2] + buf[i * 2 + 1]) >> 1);
  }
  *out_data = buf;
  *out_frames = frames;
  *out_rate = w.rate ? w.rate : PSP_OUT_RATE;
  *out_ch = 1;
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

/* Music: streamed MP3 via sceMp3, decoded on a dedicated thread into a PCM
 * ring the mixer pops from. The game thread only posts play/stop requests;
 * the music thread owns the sceMp3 handle and does all disc IO. */
typedef struct {
  volatile int active;    /* ring has a live track (mixer reads) */
  volatile uint32_t head; /* write index (frames), music thread */
  volatile uint32_t tail; /* read index (frames), mixer thread */
} MusicRing;

typedef struct {
  char path[PSP_PATH_MAX];
  volatile int loop;
  volatile int req; /* 0 none, 1 play(path staged), 2 stop */
} MusicCmd;

static int16_t* s_ring; /* PSP_RING_FRAMES stereo frames, 64-aligned */
static MusicRing s_mus;
static MusicCmd s_cmd;
static volatile int s_mus_paused;
static volatile int s_mus_gain_q15 = PSP_Q15;

static int s_ch = -1;
static SceUID s_mix_th = -1;
static SceUID s_mus_th = -1;
static volatile int s_quit;
static void* s_pcm_raw;
static int16_t* s_pcm;

/* sceMp3 handle + its buffers (music thread only after init). */
static int s_mp3 = -1;
static int s_mp3_fd = -1;
static int s_mp3_loop;         /* current track loops */
static unsigned char* s_mp3_in;  /* MP3_STREAM_BUF, 64-aligned */
static unsigned char* s_mp3_pcm; /* MP3_PCM_BUF, 64-aligned */
static int s_mp3_ch = 2;         /* channels of the current track */

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
      /* Music: pop one stereo frame from the ring the sceMp3 thread fills.
       * Single-producer (music thread writes head) / single-consumer (this
       * thread writes tail); safe under preemption because each side owns its
       * own index. Underrun just yields silence for this sample. */
      if (s_mus.active && !s_mus_paused) {
        uint32_t tail = s_mus.tail;
        if (tail != s_mus.head) {
          const int16_t* f = &s_ring[(tail & PSP_RING_MASK) << 1];
          l += (f[0] * s_mus_gain_q15) >> 15;
          r += (f[1] * s_mus_gain_q15) >> 15;
          s_mus.tail = tail + 1;
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

/* ---- Music: sceMp3 streaming (island1-ym91) ---------------------------- */

/* Feed sceMp3's compressed ring from disc. srcpos from the library is the
 * absolute file offset to read next. Returns bytes read, <=0 on EOF/fault. */
static int mp3_fill(void) {
  unsigned char* dst;
  SceInt32 towrite, srcpos;
  if (s_mp3 < 0)
    return -1;
  if (sceMp3GetInfoToAddStreamData(s_mp3, &dst, &towrite, &srcpos) < 0)
    return -1;
  if (towrite <= 0)
    return 0;
  if (sceIoLseek32(s_mp3_fd, (int)srcpos, PSP_SEEK_SET) < 0)
    return -1;
  int n = (int)sceIoRead(s_mp3_fd, dst, (SceSize)towrite);
  if (n <= 0)
    return -1;
  if (sceMp3NotifyAddStreamData(s_mp3, n) < 0)
    return -1;
  return n;
}

static void mp3_teardown(void) {
  if (s_mp3 >= 0) {
    sceMp3ReleaseMp3Handle(s_mp3);
    s_mp3 = -1;
  }
  if (s_mp3_fd >= 0) {
    sceIoClose(s_mp3_fd);
    s_mp3_fd = -1;
  }
  s_mus.active = 0;
  __sync_synchronize();
  s_mus.head = 0;
  s_mus.tail = 0;
}

/* Write decoded PCM into the ring, blocking (yielding) while it is full.
 * Mono is up-mixed to stereo so the mixer always pops interleaved stereo. */
static void ring_write(const int16_t* src, uint32_t frames, int ch) {
  while (!s_quit) {
    uint32_t head = s_mus.head;
    uint32_t free_frames = PSP_RING_FRAMES - (head - s_mus.tail);
    if (free_frames > 0) {
      uint32_t n = frames < free_frames ? frames : free_frames;
      uint32_t widx = head & PSP_RING_MASK;
      uint32_t first = PSP_RING_FRAMES - widx;
      if (first > n)
        first = n;
      int16_t* dst = &s_ring[widx << 1];
      if (ch == 2) {
        memcpy(dst, src, (size_t)first * 4);
        if (n > first)
          memcpy(&s_ring[0], src + first * 2, (size_t)(n - first) * 4);
      } else {
        for (uint32_t k = 0; k < first; k++)
          dst[k * 2] = dst[k * 2 + 1] = src[k];
        if (n > first) {
          int16_t* d0 = &s_ring[0];
          for (uint32_t k = 0; k < n - first; k++)
            d0[k * 2] = d0[k * 2 + 1] = src[first + k];
        }
      }
      __sync_synchronize();
      s_mus.head = head + n; /* publish after data is written */
      src += (size_t)n * (size_t)ch;
      frames -= n;
      if (frames == 0)
        return;
    }
    sceKernelDelayThread(1000); /* ring full: let the mixer drain */
  }
}

/* Dedicated music thread: owns the sceMp3 handle, does all disc IO and
 * decode, streams PCM into the ring. Lower priority than the mixer (0x1A vs
 * 0x12) so mixing preempts it, and the game loop is never starved. */
static int music_thread(SceSize args, void* argp) {
  (void)args;
  (void)argp;

  while (!s_quit) {
    int req = s_cmd.req;
    if (req == 1) {
      mp3_teardown();
      s_mp3_fd = sceIoOpen(s_cmd.path, PSP_O_RDONLY, 0);
      if (s_mp3_fd >= 0) {
        int size = (int)sceIoLseek32(s_mp3_fd, 0, PSP_SEEK_END);
        sceIoLseek32(s_mp3_fd, 0, PSP_SEEK_SET);
        SceMp3InitArg arg;
        memset(&arg, 0, sizeof(arg));
        arg.mp3StreamStart = 0;
        arg.mp3StreamEnd = (unsigned int)size;
        arg.mp3Buf = s_mp3_in;
        arg.mp3BufSize = MP3_STREAM_BUF;
        arg.pcmBuf = s_mp3_pcm;
        arg.pcmBufSize = MP3_PCM_BUF;
        s_mp3 = sceMp3ReserveMp3Handle(&arg);
        if (s_mp3 >= 0) {
          /* Prime the compressed ring, then init the decoder. */
          while (sceMp3CheckStreamDataNeeded(s_mp3) > 0 && mp3_fill() > 0) {
          }
          if (sceMp3Init(s_mp3) >= 0) {
            sceMp3SetLoopNum(s_mp3, s_cmd.loop ? -1 : 0);
            s_mp3_loop = s_cmd.loop ? 1 : 0;
            s_mp3_ch = sceMp3GetMp3ChannelNum(s_mp3);
            if (s_mp3_ch < 1)
              s_mp3_ch = 2;
            s_mus.active = 1; /* mixer may now pop the ring */
          } else {
            mp3_teardown();
          }
        } else {
          mp3_teardown();
        }
      }
      __sync_synchronize();
      s_cmd.req = 0;
    } else if (req == 2) {
      mp3_teardown();
      __sync_synchronize();
      s_cmd.req = 0;
    }

    if (s_mp3 >= 0 && s_mus.active) {
      if (sceMp3CheckStreamDataNeeded(s_mp3) > 0)
        mp3_fill();
      short* pcm = NULL;
      int bytes = sceMp3Decode(s_mp3, &pcm);
      if (bytes > 0 && pcm) {
        uint32_t frames = (uint32_t)bytes / (2 * (uint32_t)s_mp3_ch);
        ring_write((const int16_t*)pcm, frames, s_mp3_ch);
      } else if (bytes <= 0) {
        /* End of stream or decode fault. Loop resets to the top; a one-shot
         * goes quiet (the ring drains, then the mixer sees active==0). */
        if (s_mp3_loop) {
          sceMp3ResetPlayPosition(s_mp3);
          while (sceMp3CheckStreamDataNeeded(s_mp3) > 0 && mp3_fill() > 0) {
          }
        } else {
          s_mus.active = 0;
        }
        sceKernelDelayThread(1000);
      }
    } else {
      sceKernelDelayThread(1000); /* idle: waiting for a command */
    }
  }

  mp3_teardown();
  sceKernelExitThread(0);
  return 0;
}

/* ---- Backend entry points ---------------------------------------------- */

int backend_audio_init(void) {
  memset(s_voices, 0, sizeof(s_voices));
  memset((void*)&s_mus, 0, sizeof(s_mus));
  memset((void*)&s_cmd, 0, sizeof(s_cmd));
  s_seq = 0;
  s_quit = 0;
  s_mus_paused = 0;
  s_mus_gain_q15 = PSP_Q15;
  s_mp3 = -1;
  s_mp3_fd = -1;
  s_mp3_loop = 0;
  s_mp3_ch = 2;

  /* sceMp3 decode needs the AV codec + MP3 modules loaded once (island1-ym91).
   * Failure leaves music silent but SFX fully working. */
  if (sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC) < 0 ||
      sceUtilityLoadModule(PSP_MODULE_AV_MP3) < 0) {
    fprintf(stderr, "audio_psp: sceMp3 modules unavailable; music silent\n");
  } else if (sceMp3InitResource() < 0) {
    fprintf(stderr, "audio_psp: sceMp3InitResource failed\n");
  }

  s_ring = (int16_t*)memalign(64, (size_t)PSP_RING_FRAMES * 2 * sizeof(int16_t));
  if (!s_ring)
    return -1;
  memset(s_ring, 0, (size_t)PSP_RING_FRAMES * 2 * sizeof(int16_t));

  s_mp3_in = (unsigned char*)memalign(64, MP3_STREAM_BUF);
  s_mp3_pcm = (unsigned char*)memalign(64, MP3_PCM_BUF);
  if (!s_mp3_in || !s_mp3_pcm) {
    free(s_ring);
    s_ring = NULL;
    free(s_mp3_in);
    s_mp3_in = NULL;
    free(s_mp3_pcm);
    s_mp3_pcm = NULL;
    return -1;
  }
  memset(s_mp3_in, 0, MP3_STREAM_BUF);
  memset(s_mp3_pcm, 0, MP3_PCM_BUF);

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

  s_mus_th = sceKernelCreateThread("island_music", music_thread, PSP_MUSIC_PRIO, 0x10000, PSP_THREAD_ATTR_USER, 0);
  if (s_mus_th < 0) {
    fprintf(stderr, "audio_psp: music thread failed 0x%x\n", (unsigned)s_mus_th);
  } else {
    sceKernelStartThread(s_mus_th, 0, NULL);
  }

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
  if (s_mus_th >= 0) {
    sceKernelWaitThreadEnd(s_mus_th, NULL);
    sceKernelDeleteThread(s_mus_th);
    s_mus_th = -1;
  }
  if (s_mix_th >= 0) {
    sceKernelWaitThreadEnd(s_mix_th, NULL);
    sceKernelDeleteThread(s_mix_th);
    s_mix_th = -1;
  }
  s_mus.active = 0;
  if (s_mp3 >= 0) {
    sceMp3ReleaseMp3Handle(s_mp3);
    s_mp3 = -1;
  }
  if (s_mp3_fd >= 0) {
    sceIoClose(s_mp3_fd);
    s_mp3_fd = -1;
  }
  sceMp3TermResource();
  if (s_ch >= 0) {
    sceAudioChRelease(s_ch);
    s_ch = -1;
  }
  free(s_pcm_raw);
  s_pcm_raw = NULL;
  s_pcm = NULL;
  free(s_ring);
  s_ring = NULL;
  free(s_mp3_in);
  s_mp3_in = NULL;
  free(s_mp3_pcm);
  s_mp3_pcm = NULL;
}

int backend_audio_sfx_load(AudioSfx id, const char* relpath) {
  char path[PSP_PATH_MAX];
  if (resolve_path(relpath, ".wav", path, sizeof(path)) != 0)
    return -1;
  int16_t* data;
  uint32_t frames, rate;
  uint16_t ch;
  if (wav_load_resident(path, &data, &frames, &rate, &ch) != 0) {
    fprintf(stderr, "audio_psp: failed to load sfx %s\n", relpath);
    return -1;
  }
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

/* Stage a play request to the music thread (it owns sceMp3 + all disc IO).
 * The mixer stays live the whole time; this never blocks on the UMD. */
int backend_audio_music_play(const char* relpath, int loop) {
  char path[PSP_PATH_MAX];
  if (resolve_path(relpath, ".mp3", path, sizeof(path)) != 0)
    return -1;
  while (s_cmd.req)
    sceKernelDelayThread(100);
  snprintf(s_cmd.path, sizeof(s_cmd.path), "%s", path);
  s_cmd.loop = loop ? 1 : 0;
  s_mus_paused = 0;
  __sync_synchronize();
  s_cmd.req = 1;
  return 0;
}

void backend_audio_music_stop(void) {
  while (s_cmd.req)
    sceKernelDelayThread(100);
  __sync_synchronize();
  s_cmd.req = 2;
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
