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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <pspdisplay.h>
#include <pspgu.h>
#include <pspinit.h>
#include <pspkernel.h>

#include "game.h"
#include "input.h"
#include "render_gu.h"

#include "psp_setup.h"

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

/*
 * The init APIs (sceKernelBootFrom / sceKernelInitApitype /
 * sceKernelInitFileName) hand back error codes (0x8xxxxxxx) as their values
 * when they are unavailable — e.g. under a debugger loader — and
 * sceKernelInitFileName's "string" then points into kernel space, which a
 * user thread cannot read (AdEL). Only deref pointers inside user RAM.
 */
static int user_str(const char *p) {
  uintptr_t v = (uintptr_t)p;
  return p && v >= 0x08000000u && v < 0x0C000000u;
}

/* Case-insensitive basename-compare without dragging in strcasecmp. */
static int ieq(const char *a, const char *b) {
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
static const char *path_dev(const char *p) {
  if (!user_str(p))
    return NULL;
  static const char *devs[] = {
      "ms0:",   "ms1:",    "ms2:",    "ef0:",       "disc0:", "umd0:",
      "host0:", "flash0:", "flash1:", "kemulator:", NULL};
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
static const char *exe_kind(const char *p) {
  if (!p)
    return "(none)";
  if (!user_str(p))
    return "(not a user pointer)";
  if (!memchr(p, 0, 256))
    return "(unterminated)";
  const char *base = strrchr(p, '/');
  base = base ? base + 1 : p;
  const char *ext = strrchr(base, '.');
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
 * Boot-source diagnostics: argv, cwd, and the kernel's launch info, so
 * memstick vs disc image (vs host) and the executable flavour (EBOOT.BIN /
 * BOOT.BIN / EBOOT.PBP / prx) are visible. Printed to stdout (emulator log)
 * and mirrored to ms0:/island_boot.txt (ef0: fallback) because real
 * hardware has no console when running from a disc image.
 */
static void boot_report(int argc, char **argv) {
  char buf[1024];
  char cwd[256];
  int n = 0;
  memset(buf, 0, sizeof(buf));
  memset(cwd, 0, sizeof(cwd));
#define ADD(...)                                                               \
  do {                                                                         \
    if (n >= 0 && n < (int)sizeof(buf))                                        \
      n += snprintf(buf + n, sizeof(buf) - (size_t)n, __VA_ARGS__);            \
  } while (0)

  if (!getcwd(cwd, sizeof(cwd)))
    snprintf(cwd, sizeof(cwd), "(getcwd failed)");

  int boot = sceKernelBootFrom();
  const char *boot_n = (unsigned)boot > 0xffffu  ? "err"
                       : boot == PSP_BOOT_DISC    ? "disc"
                       : boot == PSP_BOOT_MS      ? "ms"
                       : boot == PSP_BOOT_FLASH   ? "flash"
                                                  : "?";
  int api = sceKernelInitApitype();
  const char *api_n = (unsigned)api > 0xffffu ? "err"
                      : (api & ~0xf) == 0x120 ? "disc"
                      : (api & ~0xf) == 0x140 ? "ms"
                      : (api & ~0xf) == 0x210 ? "vsh"
                                              : "?";

  const char *initfn_raw = sceKernelInitFileName();
  const char *initfn = user_str(initfn_raw) ? initfn_raw : NULL;
  const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : NULL;
  const char *src = path_dev(initfn)  ? path_dev(initfn)
                    : path_dev(argv0) ? path_dev(argv0)
                    : path_dev(cwd)   ? path_dev(cwd)
                                      : "(unknown)";

  ADD("[boot] argc=%d\n", argc);
  for (int i = 0; i < argc && i < 8; i++)
    ADD("[boot] argv[%d]=%s\n", i,
        user_str(argv[i]) ? argv[i] : "(unreadable)");
  ADD("[boot] cwd=%s\n", cwd);
  ADD("[boot] bootFrom=0x%x (%s)\n", boot, boot_n);
  ADD("[boot] initApitype=0x%x (%s)\n", api, api_n);
  ADD("[boot] initFileName=%s\n", initfn ? initfn : "(unusable)");
  ADD("[boot] argv[0] flavour=%s\n", exe_kind(argv0));
  ADD("[boot] init flavour=%s\n", exe_kind(initfn));
  ADD("[boot] running from device=%s\n", src);

  printf("%s", buf);

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
#undef ADD
}

int main(int argc, char **argv) {
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
