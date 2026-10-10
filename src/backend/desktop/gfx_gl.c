#include "gfx_gl.h"

#include <string.h>

#define GFX_STACK_MAX 8

static GfxState s_current;
static GfxState s_stack[GFX_STACK_MAX];
static int s_stack_depth = 0;
static GLuint s_bound_tex = 0;
static GLuint s_default_tex = 0;

static void setup_lights(void);

void gfx_init(void) {
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_TRUE);

  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glFrontFace(GL_CCW);

  glShadeModel(GL_SMOOTH);

  glDisable(GL_LIGHTING);
  glDisable(GL_FOG);
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_BLEND);

  /*
   * Fog defaults to GL_EXP with density 1.0, which swallows everything past
   * a couple of metres. The passes configure linear start/end distances, so
   * the mode must match or all world geometry dissolves into the sky.
   */
  glFogi(GL_FOG_MODE, GL_LINEAR);

  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  memset(&s_current, 0, sizeof(s_current));
  s_current.lighting = 0;
  s_current.fog = 0;
  s_current.texture = 0;
  s_current.blend = 0;
  s_current.depth_write = 1;
  s_current.cull = GL_BACK;
  s_current.filter = GL_NEAREST;
  s_current.tex_env = GL_MODULATE;
  s_current.fog_start = 0.0f;
  s_current.fog_end = 1.0f;
  s_current.fog_color[0] = 0.0f;
  s_current.fog_color[1] = 0.0f;
  s_current.fog_color[2] = 0.0f;
  s_current.fog_color[3] = 1.0f;

  s_stack_depth = 0;

  /*
   * A 1x1 white texture is the implicit texture whenever a draw enables
   * GL_TEXTURE_2D without binding its own. GL_MODULATE with white is the
   * identity, so untextured geometry keeps its vertex colour instead of
   * sampling the incomplete default texture object (which renders nothing).
   */
  {
    static const GLubyte white[4] = {255, 255, 255, 255};
    glGenTextures(1, &s_default_tex);
    glBindTexture(GL_TEXTURE_2D, s_default_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glBindTexture(GL_TEXTURE_2D, s_default_tex);
    s_bound_tex = s_default_tex;
  }

  setup_lights();
}

static void set_enabled(GLenum cap, int want, int* current) {
  if (want == *current)
    return;
  want ? glEnable(cap) : glDisable(cap);
  *current = want;
}

/*
 * Light table and lighting model. Fixed-function lights live in GL state, so they
 * are configured once here and every lit draw reuses them.
 */
static void setup_lights(void) {
  GLfloat ambient0[4] = {0.30f, 0.30f, 0.28f, 1.0f};
  GLfloat diffuse0[4] = {0.85f, 0.84f, 0.76f, 1.0f};
  GLfloat position0[4] = {0.5f, 1.0f, 0.3f, 0.0f};

  glLightfv(GL_LIGHT0, GL_AMBIENT, ambient0);
  glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse0);
  glLightfv(GL_LIGHT0, GL_POSITION, position0);

  GLfloat ambient1[4] = {0.14f, 0.16f, 0.22f, 1.0f};
  GLfloat diffuse1[4] = {0.22f, 0.26f, 0.34f, 1.0f};
  GLfloat position1[4] = {-0.6f, 0.4f, -0.5f, 0.0f};

  glLightfv(GL_LIGHT1, GL_AMBIENT, ambient1);
  glLightfv(GL_LIGHT1, GL_DIFFUSE, diffuse1);
  glLightfv(GL_LIGHT1, GL_POSITION, position1);

  glEnable(GL_LIGHT0);
  glEnable(GL_LIGHT1);

  glEnable(GL_COLOR_MATERIAL);
  glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

  glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_FALSE);
  glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL, GL_SEPARATE_SPECULAR_COLOR);

  /* Global ambient is a colour, so it goes through glLightModelfv, not i. */
  GLfloat global_ambient[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  glLightModelfv(GL_LIGHT_MODEL_AMBIENT, global_ambient);

  glEnable(GL_NORMALIZE);
}

void gfx_set(const GfxState* want) {
  set_enabled(GL_LIGHTING, want->lighting, &s_current.lighting);
  set_enabled(GL_FOG, want->fog, &s_current.fog);
  set_enabled(GL_TEXTURE_2D, want->texture, &s_current.texture);
  set_enabled(GL_BLEND, want->blend, &s_current.blend);

  if (want->depth_write != s_current.depth_write) {
    glDepthMask(want->depth_write ? GL_TRUE : GL_FALSE);
    s_current.depth_write = want->depth_write;
  }

  if (want->cull != s_current.cull) {
    if (want->cull == GL_NONE) {
      glDisable(GL_CULL_FACE);
    } else {
      glEnable(GL_CULL_FACE);
      glCullFace(want->cull);
    }
    s_current.cull = want->cull;
  }

  if (want->tex_env != s_current.tex_env) {
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, want->tex_env);
    s_current.tex_env = want->tex_env;
  }

  if (want->filter != s_current.filter) {
    s_current.filter = want->filter;
    if (s_bound_tex)
      gfx_bind_texture(s_bound_tex);
  }

  if (want->fog_start != s_current.fog_start) {
    glFogf(GL_FOG_START, want->fog_start);
    s_current.fog_start = want->fog_start;
  }

  if (want->fog_end != s_current.fog_end) {
    glFogf(GL_FOG_END, want->fog_end);
    s_current.fog_end = want->fog_end;
  }

  if (memcmp(want->fog_color, s_current.fog_color, sizeof(want->fog_color)) != 0) {
    glFogfv(GL_FOG_COLOR, want->fog_color);
    memcpy(s_current.fog_color, want->fog_color, sizeof(want->fog_color));
  }
}

const GfxState* gfx_current(void) {
  return &s_current;
}

void gfx_push(const GfxState* modified) {
  if (s_stack_depth < GFX_STACK_MAX)
    s_stack[s_stack_depth++] = s_current;

  gfx_set(modified);
}

void gfx_pop(void) {
  if (s_stack_depth > 0)
    gfx_set(&s_stack[--s_stack_depth]);
}

void gfx_bind_texture(GLuint tex) {
  if (tex == 0)
    tex = s_default_tex;
  s_bound_tex = tex;
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, s_current.filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, s_current.filter);
}

void gfx_screen_begin_unit(void) {
  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  glOrtho(0.0, 1.0, 0.0, 1.0, -1.0, 1.0);

  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();
}

void gfx_screen_end(void) {
  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
}
