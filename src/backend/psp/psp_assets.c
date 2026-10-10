#include "psp_assets.h"

#include <malloc.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <string.h>

#include "boot_kind.h"

/*
 * One root chain, chosen once from the boot kind main.c already determined,
 * then cached. A disc boot starts in SYSDIR and main.c chdir's to USRDIR, so
 * the relative "assets" root works there too; host/memstick boots have their
 * own absolute home and also fall back to the relative cwd. No getenv: the
 * firmware has no environment, and the boot kind fully orders the search.
 */
static const char** s_roots; /* resolved once on first lookup */

static const char** asset_roots(void) {
  if (s_roots)
    return s_roots;
  static const char* disc[] = {"disc0:/PSP_GAME/USRDIR/assets", "assets", NULL};
  static const char* host[] = {"host0:/assets", "assets", NULL};
  static const char* ms[] = {"ms0:/PSP/GAME/island/assets", "assets", NULL};
  static const char* unknown[] = {"disc0:/PSP_GAME/USRDIR/assets", "host0:/assets",
                                  "ms0:/PSP/GAME/island/assets", "assets", NULL};
  switch (psp_boot_kind()) {
    case BOOT_DISC:
      s_roots = disc;
      break;
    case BOOT_HOST:
      s_roots = host;
      break;
    case BOOT_MEMSTICK:
      s_roots = ms;
      break;
    default:
      s_roots = unknown;
      break;
  }
  return s_roots;
}

void psp_assets_init(void) {
  asset_roots();
}

SceUID psp_asset_open(const char* rel) {
  const char** roots = asset_roots();
  char path[512];
  for (int i = 0; roots[i]; i++) {
    snprintf(path, sizeof(path), "%s/%s", roots[i], rel);
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd >= 0)
      return fd;
  }
  return -1;
}

int psp_asset_path(const char* rel, char* out, int out_len) {
  const char** roots = asset_roots();
  for (int i = 0; roots[i]; i++) {
    snprintf(out, out_len, "%s/%s", roots[i], rel);
    SceUID fd = sceIoOpen(out, PSP_O_RDONLY, 0);
    if (fd >= 0) {
      sceIoClose(fd);
      return 1;
    }
  }
  return 0;
}

void* psp_asset_read(const char* rel, unsigned int* out_size) {
  SceUID fd = psp_asset_open(rel);
  if (fd < 0)
    return NULL;
  SceOff end = sceIoLseek(fd, 0, PSP_SEEK_END);
  if (end <= 0) {
    sceIoClose(fd);
    return NULL;
  }
  sceIoLseek(fd, 0, PSP_SEEK_SET);
  unsigned int size = (unsigned int)end;
  unsigned char* buf = (unsigned char*)malloc(size);
  if (!buf) {
    sceIoClose(fd);
    return NULL;
  }
  unsigned int got = 0;
  while (got < size) {
    int r = sceIoRead(fd, buf + got, size - got);
    if (r <= 0)
      break;
    got += (unsigned int)r;
  }
  sceIoClose(fd);
  if (got != size) {
    free(buf);
    return NULL;
  }
  if (out_size)
    *out_size = size;
  return buf;
}
