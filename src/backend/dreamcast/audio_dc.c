#include <dc/sound/aica_comm.h>
#include <dc/sound/sfxmgr.h>
#include <dc/sound/sound.h>
#include <dc/sound/stream.h>
#include <kos.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"

/*
 * Dreamcast audio backend (island1-rau9): a direct port of wipeout-dc's
 * proven KOS design (.scratch/wipeout-dc/src/sndwav.c + wipeout/sfx.c).
 *
 *  - Music: one KOS snd_stream of headerless interleaved Yamaha/AICA ADPCM,
 *    stereo 44.1kHz, streamed from disc via fs_read into a 32-byte-aligned
 *    64KiB buffer. A dedicated MusicPlayer thread (prio 11, 32KiB stack)
 *    calls snd_stream_poll every 25ms behind a mutex state machine. Pause
 *    is volume 0 with the stream still running (the wipeout trick).
 *  - SFX: raw 8-bit signed PCM @ 22050Hz mono, resident in AICA SARAM via
 *    snd_sfxmgr hardware voices. No software mixer. One-shots take the
 *    first-free AICA channel; reserved loops (engine hum) get a dedicated
 *    channel via snd_sfx_chn_alloc. Live vol/pan/freq updates go through a
 *    hand-built AICA_CH_CMD_UPDATE packet (wipeout's snd_sfx_update_ex --
 *    upstream KOS has no such call).
 *
 * Conversion of the WAV masters to .adpcm/.raw happens at build time
 * (scripts/convert_audio.py, island1-zozu).
 */

#define DC_SFX_RATE 22050
#define DC_MUSIC_RATE 44100
#define DC_SFX_MAX_VOL 212 /* headroom below AICA max, as in wipeout */
#define DC_PATH_MAX 512

/* ---- AICA live-channel update (wipeout snd_sfx_update_ex) ------------- */

/* Copy of KOS-internal snd_effect_t (handle -> AICA addresses); layout
 * matches kernel/arch/dreamcast/sound/snd_sfxmgr.c in this toolchain. */
typedef struct snd_effect {
  uint32_t locl, locr;
  uint32_t len;
  uint32_t rate;
  uint32_t used;
  uint32_t fmt;
  uint16_t stereo;
  LIST_ENTRY(snd_effect)
  list;
} snd_effect_t;

extern int snd_sh4_to_aica(void* packet, uint32_t size);

/* Change freq/pan/vol on a playing AICA channel without restarting it.
 * KOS main lacks this; wipeout ships it inline (sfx.c:32-61). */
static void snd_sfx_update_ex(sfx_play_data_t* data) {
  snd_effect_t* t = (snd_effect_t*)data->idx;
  AICA_CMDSTR_CHANNEL(tmp, cmd, chan);
  int size = (int)t->len;
  if (size >= 65535)
    size = 65534;

  cmd->cmd = AICA_CMD_CHAN;
  cmd->timestamp = 0;
  cmd->size = AICA_CMDSTR_CHANNEL_SIZE;
  cmd->cmd_id = data->chn;

  chan->cmd = AICA_CH_CMD_UPDATE | AICA_CH_UPDATE_SET_FREQ |
              AICA_CH_UPDATE_SET_PAN | AICA_CH_UPDATE_SET_VOL;
  chan->base = t->locl;
  chan->type = t->fmt;
  chan->length = size;
  chan->loop = data->loop;
  chan->loopstart = data->loopstart;
  chan->loopend = data->loopend ? data->loopend : (uint32_t)size;
  chan->freq = data->freq > 0 ? data->freq : (int)t->rate;
  chan->vol = data->vol;
  chan->pan = data->pan;

  snd_sh4_to_aica(tmp, cmd->size);
}

/* ---- Path resolution -------------------------------------------------- */

static const char* asset_root(void) {
  const char* dir = getenv("AUDIO_ASSET_DIR");
  if (dir && *dir)
    return dir;
  dir = getenv("ASSET_DIR");
  if (dir && *dir)
    return dir;
  return "/cd/assets";
}

/* Probe the root chain for relpath+suffix; 0 = found. */
static int resolve_path(const char* relpath, const char* suffix, char* out, int out_len) {
  static const char* roots[3];
  roots[0] = getenv("AUDIO_ASSET_DIR");
  roots[1] = getenv("ASSET_DIR");
  roots[2] = "/cd/assets";

  for (int i = 0; i < 3; i++) {
    if (!roots[i] || !*roots[i])
      continue;
    snprintf(out, out_len, "%s/%s%s", roots[i], relpath, suffix);
    file_t f = fs_open(out, O_RDONLY);
    if (f != FILEHND_INVALID) {
      fs_close(f);
      return 0;
    }
  }
  (void)asset_root;
  return -1;
}

/* ---- SFX: sfxmgr hardware voices -------------------------------------- */

static sfxhnd_t s_sfx_hnd[AUDIO_SFX_COUNT];
static uint32_t s_sfx_len[AUDIO_SFX_COUNT]; /* samples, for loopend */

/* Per-AICA-channel playback state, indexed by channel (voice id). */
static sfx_play_data_t s_chn_data[64];
static int s_chn_used[64];

static int dc_sfx_load(AudioSfx id, const char* relpath) {
  char path[DC_PATH_MAX];
  if (resolve_path(relpath, ".raw", path, sizeof(path)) != 0)
    return -1;

  file_t f = fs_open(path, O_RDONLY);
  if (f == FILEHND_INVALID)
    return -1;
  size_t len = fs_total(f);
  if (len == 0 || len > 65534) {
    /* sfxmgr caps at 65534 samples; refuse rather than truncate. */
    fprintf(stderr, "audio_dc: %s bad size %u\n", path, (unsigned)len);
    fs_close(f);
    return -1;
  }
  char* buf = malloc(len);
  if (!buf) {
    fs_close(f);
    return -1;
  }
  fs_read(f, buf, len);
  fs_close(f);

  /* snd_sfx_load_raw_buf copies into SARAM (spu_memload_sq), so the host
   * buffer is free after this returns. */
  s_sfx_hnd[id] = snd_sfx_load_raw_buf(buf, len, DC_SFX_RATE, 8, 1);
  free(buf);
  if (s_sfx_hnd[id] == SFXHND_INVALID)
    return -1;
  s_sfx_len[id] = (uint32_t)len; /* mono 8-bit: 1 byte = 1 sample */
  return 0;
}

static int dc_sfx_play(AudioSfx id, float volume, float pan, float pitch, int loop) {
  if (id <= AUDIO_SFX_NONE || id >= AUDIO_SFX_COUNT)
    return -1;
  sfxhnd_t h = s_sfx_hnd[id];
  if (h == SFXHND_INVALID)
    return -1;

  sfx_play_data_t data;
  memset(&data, 0, sizeof(data));
  data.idx = h;
  data.loop = loop ? 1 : 0;
  data.loopstart = 0;
  data.loopend = loop ? s_sfx_len[id] : 0;

  int vol_i = (int)(volume * (float)DC_SFX_MAX_VOL);
  if (vol_i > 255)
    vol_i = 255;
  if (vol_i < 0)
    vol_i = 0;
  int pan_i = 127 + (int)(pan * 128.0f);
  if (pan_i < 0)
    pan_i = 0;
  if (pan_i > 255)
    pan_i = 255;
  float pr = pitch > 0.0f ? pitch : 1.0f;
  int freq = (int)(pr * (float)DC_SFX_RATE);

  data.vol = vol_i;
  data.pan = pan_i;
  data.freq = freq;

  if (loop) {
    data.chn = snd_sfx_chn_alloc();
    if (data.chn < 0 || data.chn >= 64)
      return -1;
  } else {
    data.chn = -1;
  }

  int chn = snd_sfx_play_ex(&data);
  if (chn < 0 || chn >= 64)
    return -1;

  s_chn_data[chn] = data;
  s_chn_data[chn].chn = chn;
  s_chn_used[chn] = 1;
  return chn;
}

static void dc_voice_update(int voice, float volume, float pan, float pitch) {
  if (voice < 0 || voice >= 64 || !s_chn_used[voice])
    return;
  sfx_play_data_t* d = &s_chn_data[voice];

  int vol_i = (int)(volume * (float)DC_SFX_MAX_VOL);
  if (vol_i > 255)
    vol_i = 255;
  if (vol_i < 0)
    vol_i = 0;
  int pan_i = 127 + (int)(pan * 128.0f);
  if (pan_i < 0)
    pan_i = 0;
  if (pan_i > 255)
    pan_i = 255;
  float pr = pitch > 0.0f ? pitch : 1.0f;

  d->vol = vol_i;
  d->pan = pan_i;
  d->freq = (int)(pr * (float)DC_SFX_RATE);
  snd_sfx_update_ex(d);
}

static void dc_voice_stop(int voice, int loop) {
  if (voice < 0 || voice >= 64 || !s_chn_used[voice])
    return;
  snd_sfx_stop(voice);
  if (loop)
    snd_sfx_chn_free(voice);
  s_chn_used[voice] = 0;
}

/* ---- Music: sndstream + MusicPlayer thread (wipeout sndwav.c) --------- */

#define MSTAT_NULL 0
#define MSTAT_READY 1
#define MSTAT_STREAMING 2
#define MSTAT_PAUSING 3
#define MSTAT_STOPPING 4
#define MSTAT_RESUMING 5

static snd_stream_hnd_t s_msnd = SND_STREAM_INVALID;
static file_t s_mfile = FILEHND_INVALID;
static uint8_t* s_mbuf = NULL; /* 32-byte aligned SH4->AICA staging buffer */
static volatile int s_mstatus = MSTAT_NULL;
static volatile int s_mthread_run = 0;
static uint32_t s_mloop = 0;
static uint32_t s_mvol = 0;         /* 0..255 */
static uint32_t s_mchannels = 2;    /* ADPCM masters are stereo */
static uint32_t s_mdata_offset = 0; /* headerless: always 0 */
static kthread_t* s_mthread = NULL;
static mutex_t s_mmtx;

/* The stream fill callback: fs_read ADPCM from disc, wrap on loop. */
static void* dc_music_callback(snd_stream_hnd_t hnd, int req, int* done) {
  (void)hnd;
  ssize_t read = fs_read(s_mfile, s_mbuf, req);
  if (read == -1) {
    snd_stream_stop(s_msnd);
    s_mstatus = MSTAT_READY;
    return NULL;
  }
  if (read != req) {
    fs_seek(s_mfile, s_mdata_offset, SEEK_SET);
    if (s_mloop) {
      ssize_t read2 = fs_read(s_mfile, s_mbuf, req);
      if (read2 == -1) {
        snd_stream_stop(s_msnd);
        s_mstatus = MSTAT_READY;
        return NULL;
      }
    } else {
      snd_stream_stop(s_msnd);
      s_mstatus = MSTAT_READY;
      return NULL;
    }
  }
  *done = req;
  return s_mbuf;
}

/* The MusicPlayer thread: mutex-guarded state machine, 25ms poll. */
static void* dc_music_thread(void* param) {
  (void)param;
  while (s_mthread_run) {
    mutex_lock(&s_mmtx);
    switch (s_mstatus) {
      case MSTAT_RESUMING:
        snd_stream_volume(s_msnd, (int)s_mvol);
        snd_stream_start_adpcm(s_msnd, DC_MUSIC_RATE, (int)s_mchannels - 1);
        snd_stream_volume(s_msnd, (int)s_mvol);
        s_mstatus = MSTAT_STREAMING;
        break;
      case MSTAT_PAUSING:
        snd_stream_stop(s_msnd);
        s_mstatus = MSTAT_READY;
        break;
      case MSTAT_STOPPING:
        snd_stream_stop(s_msnd);
        if (s_mfile != FILEHND_INVALID)
          fs_seek(s_mfile, s_mdata_offset, SEEK_SET);
        s_mstatus = MSTAT_READY;
        break;
      case MSTAT_STREAMING:
        snd_stream_poll(s_msnd);
        break;
      default:
        break;
    }
    mutex_unlock(&s_mmtx);
    thd_sleep(25);
  }
  return NULL;
}

static void dc_music_destroy(void) {
  mutex_lock(&s_mmtx);
  if (s_msnd != SND_STREAM_INVALID) {
    snd_stream_destroy(s_msnd);
    s_msnd = SND_STREAM_INVALID;
  }
  if (s_mfile != FILEHND_INVALID) {
    fs_close(s_mfile);
    s_mfile = FILEHND_INVALID;
  }
  s_mstatus = MSTAT_NULL;
  s_mvol = 0;
  mutex_unlock(&s_mmtx);
}

static int dc_music_play(const char* relpath, int loop) {
  char path[DC_PATH_MAX];
  if (resolve_path(relpath, ".adpcm", path, sizeof(path)) != 0)
    return -1;

  dc_music_destroy();

  file_t f = fs_open(path, O_RDONLY);
  if (f == FILEHND_INVALID)
    return -1;
  snd_stream_hnd_t h = snd_stream_alloc(dc_music_callback,
                                        SND_STREAM_BUFFER_MAX);
  if (h == SND_STREAM_INVALID) {
    fs_close(f);
    return -1;
  }
  if (!s_mbuf) {
    s_mbuf = memalign(32, SND_STREAM_BUFFER_MAX);
    if (!s_mbuf) {
      snd_stream_destroy(h);
      fs_close(f);
      return -1;
    }
  }

  mutex_lock(&s_mmtx);
  s_msnd = h;
  s_mfile = f;
  s_mloop = loop ? 1 : 0;
  s_mchannels = 2;
  s_mdata_offset = 0;
  if (s_mvol == 0)
    s_mvol = 180; /* default music level until core scales it */
  fs_seek(s_mfile, 0, SEEK_SET);
  snd_stream_volume(s_msnd, (int)s_mvol);
  s_mstatus = MSTAT_READY;
  mutex_unlock(&s_mmtx);

  s_mstatus = MSTAT_RESUMING;
  return 0;
}

static void dc_music_stop(void) {
  if (s_msnd == SND_STREAM_INVALID)
    return;
  s_mstatus = MSTAT_STOPPING;
}

static void dc_music_pause(int paused) {
  if (s_msnd == SND_STREAM_INVALID)
    return;
  if (paused) {
    if (s_mstatus == MSTAT_STREAMING || s_mstatus == MSTAT_RESUMING)
      s_mstatus = MSTAT_PAUSING;
  } else {
    if (s_mstatus == MSTAT_READY || s_mstatus == MSTAT_PAUSING)
      s_mstatus = MSTAT_RESUMING;
  }
}

static void dc_music_volume(float v) {
  if (v < 0.0f)
    v = 0.0f;
  if (v > 1.0f)
    v = 1.0f;
  s_mvol = (uint32_t)(v * 255.0f);
  if (s_msnd != SND_STREAM_INVALID)
    snd_stream_volume(s_msnd, (int)s_mvol);
}

/* ---- Backend entry points --------------------------------------------- */

int backend_audio_init(void) {
  memset(s_sfx_hnd, 0, sizeof(s_sfx_hnd));
  memset(s_sfx_len, 0, sizeof(s_sfx_len));
  memset(s_chn_data, 0, sizeof(s_chn_data));
  memset(s_chn_used, 0, sizeof(s_chn_used));

  /* snd_stream_init implicitly brings up snd_init for sfxmgr too. */
  if (snd_stream_init() < 0) {
    fprintf(stderr, "audio_dc: snd_stream_init failed\n");
    return -1;
  }

  mutex_init(&s_mmtx, MUTEX_TYPE_NORMAL);
  s_mthread_run = 1;
  kthread_attr_t attr;
  memset(&attr, 0, sizeof(attr));
  attr.create_detached = 0;
  attr.stack_size = 32768;
  attr.stack_ptr = NULL;
  attr.prio = 11;
  attr.label = "MusicPlayer";
  s_mthread = thd_create_ex(&attr, dc_music_thread, NULL);
  if (!s_mthread) {
    fprintf(stderr, "audio_dc: MusicPlayer thread failed\n");
    s_mthread_run = 0;
    snd_stream_shutdown();
    return -1;
  }
  return 0;
}

void backend_audio_shutdown(void) {
  /* Join the stream thread before tearing the stream system down (wipeout
   * never does this -- we do). */
  s_mthread_run = 0;
  if (s_mthread) {
    thd_join(s_mthread, NULL);
    s_mthread = NULL;
  }
  dc_music_destroy();
  if (s_mbuf) {
    free(s_mbuf);
    s_mbuf = NULL;
  }
  snd_sfx_unload_all();
  snd_stream_shutdown();
}

int backend_audio_sfx_load(AudioSfx id, const char* relpath) {
  return dc_sfx_load(id, relpath);
}

void backend_audio_sfx_unload_all(void) {
  for (int i = 0; i < 64; i++) {
    if (s_chn_used[i]) {
      snd_sfx_stop(i);
      s_chn_used[i] = 0;
    }
  }
  snd_sfx_unload_all();
  for (int i = 0; i < AUDIO_SFX_COUNT; i++)
    s_sfx_hnd[i] = SFXHND_INVALID;
}

int backend_audio_sfx_play(AudioSfx id, float volume, float pan, float pitch, int loop) {
  return dc_sfx_play(id, volume, pan, pitch, loop);
}

void backend_audio_voice_update(int voice, float volume, float pan, float pitch) {
  dc_voice_update(voice, volume, pan, pitch);
}

void backend_audio_voice_stop(int voice, int loop) {
  dc_voice_stop(voice, loop);
}

int backend_audio_music_play(const char* relpath, int loop) {
  return dc_music_play(relpath, loop);
}

void backend_audio_music_stop(void) {
  dc_music_stop();
}

void backend_audio_music_pause(int paused) {
  dc_music_pause(paused);
}

void backend_audio_music_volume(float v) {
  dc_music_volume(v);
}
