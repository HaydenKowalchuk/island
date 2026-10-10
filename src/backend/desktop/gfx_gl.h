#ifndef ISLAND_BACKEND_DESKTOP_GFX_GL_H
#define ISLAND_BACKEND_DESKTOP_GFX_GL_H

#include <glad/glad.h>

/*
 * Fixed-function state descriptor. Draws declare the state they need; gfx_set()
 * applies only what differs from the current state, so a draw can never inherit
 * a surprise from the draw before it.
 */
typedef struct {
  int lighting;
  int fog;
  int texture;
  int blend;
  int depth_write;
  int cull;    /* GL_BACK, GL_NONE */
  int filter;  /* GL_NEAREST, GL_LINEAR */
  int tex_env; /* GL_MODULATE, GL_REPLACE, GL_DECAL */
  float fog_start;
  float fog_end;
  float fog_color[4];
} GfxState;

/* Global defaults: depth test on, cull back faces, lighting/fog/texture/blend off. */
void gfx_init(void);

/* Applies the delta between the current state and *want. */
void gfx_set(const GfxState* want);

/* Current state, for callers that want to copy it and change a field or two. */
const GfxState* gfx_current(void);

/* Save the current state, then apply *modified. gfx_pop() restores the save. */
void gfx_push(const GfxState* modified);
void gfx_pop(void);

/*
 * Binds a texture and applies the current state's filter to it. GL 1.x has no
 * per-unit sampler objects, so filtering is a property of the bound texture.
 */
void gfx_bind_texture(GLuint tex);

/*
 * Normalised screen-space drawing: sets an ortho projection over [0,1]x[0,1]
 * with the origin at the bottom-left. Pair every begin with an end.
 */
void gfx_screen_begin_unit(void);
void gfx_screen_end(void);

#endif
