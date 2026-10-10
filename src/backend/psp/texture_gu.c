#include "texture_gu.h"

#include <malloc.h> /* memalign (newlib) */
#include <pspgu.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psp_assets.h"

/*
 * PNG only keeps the vendored decoder small; all assets are 8-bit RGBA PNGs.
 * STBI_ONLY_PNG leaves a couple of unused helpers in the header, so silence
 * that third-party warning for the include only.
 */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/*
 * PSP texture loader. Decodes a PNG with stb_image, converts it to the GE's
 * 32-bit ABGR layout (GU_PSM_8888) and keeps it in cache-flushed main memory —
 * one upload per name, sampled with NEAREST for the low-poly look.
 *
 * Geometry is built from untextured vertex colours, so this path is only used
 * for the odd textured prop (assets/textures/parcel_32.png).
 */
#define MAX_TEXTURES 64

typedef struct {
  unsigned int* pixels;
  int w, h, tbw;
  unsigned int psm;
} GuTexture;

static GuTexture s_tex[MAX_TEXTURES];
static int s_tex_count = 0;

static int is_pow2(int v) {
  return v > 0 && (v & (v - 1)) == 0;
}

static TextureId load_texture(const char* name) {
  /* Resolve through the boot-kind-aware root chain (psp_assets.c) and read
   * the whole file via sceIo -- same absolute-path read that used to live in
   * load_disc, now shared with the audio loader. No getenv on PSP. */
  unsigned int size = 0;
  unsigned char* raw = (unsigned char*)psp_asset_read(name, &size);
  if (!raw || size == 0) {
    printf("texture: cannot load %s\n", name);
    return 0;
  }
  int comp = 0;
  int w = 0, h = 0;
  unsigned char* data = stbi_load_from_memory(raw, (int)size, &w, &h, &comp, 4);
  free(raw);
  if (!data) {
    printf("texture: cannot load %s\n", name);
    return 0;
  }

  if (!is_pow2(w) || !is_pow2(h)) {
    printf("texture: %s is %dx%d (GE needs power-of-two)\n", name, w, h);
    stbi_image_free(data);
    return 0;
  }

  int count = w * h;
  unsigned int* pixels = (unsigned int*)memalign(16, (size_t)count * 4);
  if (!pixels) {
    stbi_image_free(data);
    return 0;
  }

  /* stb gives R,G,B,A bytes; the GE wants an ABGR word (little-endian R G B A).
   */
  for (int i = 0; i < count; i++) {
    unsigned char r = data[i * 4 + 0];
    unsigned char g = data[i * 4 + 1];
    unsigned char b = data[i * 4 + 2];
    unsigned char a = data[i * 4 + 3];
    pixels[i] = ((unsigned int)a << 24) | ((unsigned int)b << 16) |
                ((unsigned int)g << 8) | (unsigned int)r;
  }
  stbi_image_free(data);

  /* GE reads through its own cache path: make sure the pixels are visible. */
  sceKernelDcacheWritebackRange(pixels, (unsigned int)(count * 4));

  if (s_tex_count >= MAX_TEXTURES)
    return 0;

  int idx = s_tex_count++;
  s_tex[idx].pixels = pixels;
  s_tex[idx].w = w;
  s_tex[idx].h = h;
  s_tex[idx].tbw = w; /* buffer width in pixels; == w for pow2 */
  s_tex[idx].psm = GU_PSM_8888;
  return (TextureId)(idx + 1);
}

void texture_gu_apply(TextureId id) {
  int idx = id - 1;
  if (idx < 0 || idx >= s_tex_count)
    return;

  GuTexture* t = &s_tex[idx];
  sceGuTexMode(t->psm, 0, 0, 0);
  sceGuTexImage(0, t->w, t->h, t->tbw, t->pixels);
  sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
  sceGuTexFilter(GU_NEAREST, GU_NEAREST);
  sceGuTexWrap(GU_REPEAT, GU_REPEAT);
  sceGuTexFlush();
}

void texture_gu_init(void) {
  texture_set_loader(load_texture);
}

void texture_gu_shutdown(void) {
  for (int i = 0; i < s_tex_count; i++)
    free(s_tex[i].pixels);
  s_tex_count = 0;
}
