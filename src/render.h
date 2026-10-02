#ifndef RENDER_H
#define RENDER_H

#include <glad/glad.h>

void render_init(void);
void render_shutdown(void);

/* Clears the framebuffer using the current clear color. */
void render_begin_frame(void);

/* Sets the fixed-function projection from a column-major mat4. */
void render_set_projection(const float* mat4);

/* Sets the fixed-function modelview from a column-major mat4. */
void render_set_modelview(const float* mat4);

/* Draws the placeholder ground grid centred on the origin. */
void render_ground(void);

/* Draws the startup axis gizmo in model space. */
void render_axes(float len);

#endif