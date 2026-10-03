#ifndef RENDER_H
#define RENDER_H

#include <glad/glad.h>

/* Sky colour doubles as the fog colour, so the horizon has no visible seam. */
#define SKY_COLOR_R 0.45f
#define SKY_COLOR_G 0.72f
#define SKY_COLOR_B 0.90f

#define FOG_START 40.0f
#define FOG_END   220.0f

void render_init(void);
void render_shutdown(void);

/* Clears the framebuffer and runs every pass in order. */
void render_frame(void);

/* Matrix setup, called before the passes. */
void render_set_projection(const float* mat);
void render_set_modelview(const float* mat);

#endif