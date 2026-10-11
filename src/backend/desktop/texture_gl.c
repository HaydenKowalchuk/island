#include "texture_gl.h"

#include <glad/glad.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * PNG only keeps the vendored decoder small; all assets are 8-bit RGBA PNGs.
 * STBI_ONLY_PNG leaves a couple of unused helpers in the header, so silence
 * that third-party warning for the include only.
 */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "bundle.h"
#include "texture.h"

/*
 * Desktop texture loader. Resolves a name to <ASSET_DIR>/<name>, decodes it with
 * stb_image, and uploads a GL_NEAREST texture. Handles are GLuints; we keep a
 * registry so render shutdown can delete them. Core caches the name->handle map.
 */
#define MAX_TEXTURES 64
static GLuint s_tex[MAX_TEXTURES];
static int s_tex_count = 0;

static const char* asset_root(void) {
  const char* dir = getenv("ASSET_DIR");
  if (dir && *dir)
    return dir;
  /* No env var: if we're inside a macOS .app bundle, use its Resources. */
  const char* bundled = bundle_assets_root();
  if (bundled)
    return bundled;
  return "assets";
}

static TextureId load_texture(const char* name) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s", asset_root(), name);

  int w = 0, h = 0, comp = 0;
  unsigned char* data = stbi_load(path, &w, &h, &comp, 4);
  if (!data) {
    fprintf(stderr, "texture: cannot load %s (%s)\n", path, stbi_failure_reason());
    return 0;
  }

  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
  glBindTexture(GL_TEXTURE_2D, 0);

  stbi_image_free(data);

  if (s_tex_count < MAX_TEXTURES)
    s_tex[s_tex_count++] = tex;

  return (TextureId)tex;
}

void texture_gl_init(void) {
  texture_set_loader(load_texture);
}

void texture_gl_shutdown(void) {
  for (int i = 0; i < s_tex_count; i++)
    glDeleteTextures(1, &s_tex[i]);
  s_tex_count = 0;
}
