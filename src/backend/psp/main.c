/*
 * island -- PSP backend
 *
 * Owns the GE (sceGu) display, the controller and the fixed-timestep loop. Each
 * frame it polls the pad into the core input contract, advances the core
 * simulation, records a frame through the RenderAPI and presents it on the GE.
 *
 * Controls (island1-rggc):
 *   Cross throttle   Circle brake      L boost        Square interact
 *   Triangle car     Start pause       Home quit      nub steer / pan
 */

#include <errno.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "boot_kind.h"
#include "game.h"
#include "input.h"
#include "psp_setup.h"
#include "render_gu.h"

#define SCR_W 480
#define SCR_H 272

/* Fixed logic tick. Physics and game rules run at exactly this rate. */
#define TICK_HZ 60.0f
#define TICK (1.0f / TICK_HZ)

/* Cap on catch-up ticks per frame; beyond this the backlog is dropped. */
#define MAX_TICKS 5

/* Longest frame delta we accept. */
#define MAX_FRAME_DT 0.25f

static float now_seconds(void) {
  return (float)((double)sceKernelGetSystemTimeWide() / 1000000.0);
}

/* Enum + rationale live in boot_kind.h; storage is shared with texture_gu. */
static enum boot_kind s_boot_kind = BOOT_UNKNOWN;

enum boot_kind psp_boot_kind(void) {
  return s_boot_kind;
}

void psp_set_boot_kind(enum boot_kind kind) {
  s_boot_kind = kind;
}

static enum boot_kind kind_of(const char* dev) {
  if (!dev)
    return BOOT_UNKNOWN;
  if (!strcmp(dev, "host0:"))
    return BOOT_HOST;
  if (!strcmp(dev, "disc0:") || !strcmp(dev, "umd0:"))
    return BOOT_DISC;
  if (!strncmp(dev, "ms", 2) || !strcmp(dev, "ef0:"))
    return BOOT_MEMSTICK;
  return BOOT_UNKNOWN;
}

static const char* kind_name(enum boot_kind k) {
  switch (k) {
    case BOOT_HOST:
      return "host (dev tree)";
    case BOOT_DISC:
      return "disc (umd/iso)";
    case BOOT_MEMSTICK:
      return "memstick (pbp)";
    default:
      return "unknown";
  }
}

/* argv strings live in user RAM; reject NULL, kernel pointers and error
 * codes masquerading as pointers before dereferencing. */
static int user_str(const char* p) {
  uintptr_t v = (uintptr_t)p;
  return p && v >= 0x08000000u && v < 0x0C000000u;
}

/* Case-insensitive basename-compare without dragging in strcasecmp. */
static int ieq(const char* a, const char* b) {
  for (; *a && *b; a++, b++) {
    int ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z')
      ca += 'a' - 'A';
    if (cb >= 'A' && cb <= 'Z')
      cb += 'a' - 'A';
    if (ca != cb)
      return 0;
  }
  return *a == *b;
}

/* Device prefix of a path, or NULL if it does not look like one. */
static const char* path_dev(const char* p) {
  if (!user_str(p))
    return NULL;
  static const char* devs[] = {
      "ms0:", "ms1:", "ms2:", "ef0:", "disc0:", "umd0:", "host0:", "flash0:", "flash1:", "kemulator:", NULL};
  for (int i = 0; devs[i]; i++) {
    if (!devs[i]) {
      return NULL;
    }
    if (!strncmp(p, devs[i], strlen(devs[i]))) {
      return devs[i];
    }
  }
  return NULL;
}

/* Executable flavour from a path: EBOOT.BIN (disc game), BOOT.BIN (disc
 * homebrew), EBOOT.PBP (PBP), *.prx (raw module), or the plain basename. */
static const char* exe_kind(const char* p) {
  if (!p)
    return "(none)";
  if (!user_str(p))
    return "(not a user pointer)";
  if (!memchr(p, 0, 256))
    return "(unterminated)";
  const char* base = strrchr(p, '/');
  base = base ? base + 1 : p;
  const char* ext = strrchr(base, '.');
  if (ieq(base, "EBOOT.BIN"))
    return "EBOOT.BIN (disc game bootstrap)";
  if (ieq(base, "BOOT.BIN"))
    return "BOOT.BIN (disc homebrew bootstrap)";
  if (ieq(base, "EBOOT.PBP"))
    return "EBOOT.PBP (PBP)";
  if (ext && ieq(ext, ".prx"))
    return "prx (raw module)";
  return base;
}

/*
 * Boot-source diagnostics: argv, cwd and the boot enum (host vs disc/iso vs
 * memstick), plus the executable flavour (EBOOT.BIN / BOOT.BIN / EBOOT.PBP /
 * prx). Printed to stdout (emulator/debugger log) and mirrored to
 * host0:/ms0:/ef0:/island_boot.txt because real hardware has no console
 * when running from a disc image.
 */
static enum boot_kind boot_report(int argc, char** argv) {
  char buf[2048];
  char cwd[256];
  int n = 0;
  memset(buf, 0, sizeof(buf));
  memset(cwd, 0, sizeof(cwd));
#define ADD(...)                                                    \
  do {                                                              \
    if (n >= 0 && n < (int)sizeof(buf))                             \
      n += snprintf(buf + n, sizeof(buf) - (size_t)n, __VA_ARGS__); \
  } while (0)

  if (!getcwd(cwd, sizeof(cwd)))
    snprintf(cwd, sizeof(cwd), "(getcwd failed)");

  const char* argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : NULL;
  const char* src_dev = path_dev(argv0) ? path_dev(argv0) : path_dev(cwd);
  enum boot_kind kind = kind_of(src_dev);
  psp_set_boot_kind(kind);

  ADD("[boot] argc=%d\n", argc);
  for (int i = 0; i < argc && i < 8; i++)
    ADD("[boot] argv[%d]=%s\n", i, user_str(argv[i]) ? argv[i] : "(unreadable)");
  ADD("[boot] cwd=%s\n", cwd);
  ADD("[boot] kind=%s\n", kind_name(kind));
  ADD("[boot] argv[0] flavour=%s\n", exe_kind(argv0));
  ADD("[boot] running from device=%s\n", src_dev ? src_dev : "(unknown)");

  /* A disc boot starts in SYSDIR next to EBOOT.BIN; assets live in USRDIR.
   * Chdir here so the result lands in the report file too (hardware has no
   * console), then probe the relative texture root from where we ended up. */
  if (kind == BOOT_DISC) {
    ADD("[boot] chdir disc0:/PSP_GAME/USRDIR %s\n",
        chdir("disc0:/PSP_GAME/USRDIR") == 0 ? "ok" : "failed");
    memset(cwd, 0, sizeof(cwd));
    if (getcwd(cwd, sizeof(cwd)))
      ADD("[boot] cwd now=%s\n", cwd);
  }
  /* Ground truth: rel vs abs, stdio vs raw sceIo, plus what the firmware
   * directory reader actually sees on the disc. */
  static const char* rel_p = "assets/textures/parcel_32.png";
  static const char* abs_p =
      "disc0:/PSP_GAME/USRDIR/assets/textures/parcel_32.png";
  static const char* ctl_p = "disc0:/PSP_GAME/SYSDIR/EBOOT.BIN";
  FILE* pf;

  errno = 0;
  pf = fopen(rel_p, "rb");
  ADD("[boot] probe rel  stdio=%s errno=%d\n", pf ? "ok" : "missing", errno);
  if (pf)
    fclose(pf);

  errno = 0;
  pf = fopen(abs_p, "rb");
  ADD("[boot] probe abs  stdio=%s errno=%d\n", pf ? "ok" : "missing", errno);
  if (pf)
    fclose(pf);

  errno = 0;
  pf = fopen(ctl_p, "rb");
  ADD("[boot] probe ctl  stdio=%s errno=%d\n", pf ? "ok" : "missing", errno);
  if (pf)
    fclose(pf);

  int sfd = sceIoOpen(abs_p, PSP_O_RDONLY, 0);
  if (sfd >= 0) {
    ADD("[boot] probe abs  sceIo=ok\n");
    sceIoClose(sfd);
  } else {
    ADD("[boot] probe abs  sceIo=err 0x%x\n", (unsigned)sfd);
  }

  errno = 0;
  pf = fopen("disc0:/PSP_GAME/USRDIR/assets/textures/PARCEL_32.PNG", "rb");
  ADD("[boot] probe upper stdio=%s errno=%d\n", pf ? "ok" : "missing", errno);
  if (pf)
    fclose(pf);

  errno = 0;
  pf = fopen("disc0:/PSP_GAME/USRDIR/assets/textures/parcel_32.png;1", "rb");
  ADD("[boot] probe vers  stdio=%s errno=%d\n", pf ? "ok" : "missing", errno);
  if (pf)
    fclose(pf);

  SceIoStat gst;
  memset(&gst, 0, sizeof(gst));
  int gsr = sceIoGetstat(abs_p, &gst);
  if (gsr >= 0)
    ADD("[boot] probe getstat abs=ok\n");
  else
    ADD("[boot] probe getstat abs=err 0x%x\n", (unsigned)gsr);

  int dd = sceIoDopen("disc0:/PSP_GAME/USRDIR/assets/textures");
  if (dd >= 0) {
    SceIoDirent de;
    int shown = 0;
    ADD("[boot] probe dopen textures=ok\n");
    memset(&de, 0, sizeof(de));
    while (sceIoDread(dd, &de) > 0 && shown < 4) {
      ADD("[boot]   dname=%s\n", de.d_name);
      memset(&de, 0, sizeof(de));
      shown++;
    }
    sceIoDclose(dd);
  } else {
    ADD("[boot] probe dopen textures=err 0x%x\n", (unsigned)dd);
  }

  printf("%s", buf);
#if 0
  /* host0: (psplink / emulator host fs) lands on the PC; ms0:/ef0: are the
   * stick. Write every one that opens so the report shows up wherever the
   * session actually lives. */
  static const char *const outs[] = {
      "host0:/island_boot.txt", "ms0:/island_boot.txt", "ef0:/island_boot.txt"};
  int wrote = 0;
  for (int i = 0; i < 3; i++) {
    FILE *f = fopen(outs[i], "w");
    if (!f)
      continue;
    fputs(buf, f);
    fclose(f);
    printf("[boot] wrote %s\n", outs[i]);
    wrote = 1;
  }
  if (!wrote)
    printf("[boot] (nowhere to write island_boot.txt)\n");
#endif
#undef ADD
  return kind;
}

int main(int argc, char** argv) {
  psp_setup_callbacks();
  setbuf(stdout, NULL);

  boot_report(argc, argv);

  input_init();
  backend_input_init();

  game_resize(SCR_W, SCR_H);

  /* render_init installs the texture loader, so content built in game_init
   * can request textures and have them uploaded immediately. */
  render_init();
  game_init();

  /* HUD/text rendering corrupts on real hardware; off until it is sorted. */
  // game_set_hud(0);

  float last = now_seconds();
  float accumulator = 0.0f;

  while (!psp_done) {
    float now = now_seconds();
    float frame_dt = now - last;
    last = now;

    if (frame_dt < 0.0f)
      frame_dt = 0.0f;
    if (frame_dt > MAX_FRAME_DT)
      frame_dt = MAX_FRAME_DT;

    backend_input_poll();
    if (input_held(ACT_QUIT))
      break;

    accumulator += frame_dt;

    int ticks = 0;
    while (accumulator >= TICK && ticks < MAX_TICKS) {
      input_begin_frame();

      game_tick(TICK);

      accumulator -= TICK;
      ticks++;
    }

    if (ticks == MAX_TICKS)
      accumulator = 0.0f; /* Drop the backlog rather than spiral. */

    game_render(frame_dt);
    render_present(SCR_W, SCR_H);
  }

  game_shutdown();
  render_shutdown();
  sceKernelExitGame();
  return 0;
}
