#ifndef ISLAND_BACKEND_DESKTOP_RENDER_GL_H
#define ISLAND_BACKEND_DESKTOP_RENDER_GL_H

/*
 * Desktop GL backend. It owns no game state: it initialises fixed-function GL
 * once, then every frame turns the frame recorded by core (r_frame()) into
 * gfx/GL calls.
 */
void render_init(void);
void render_shutdown(void);
void render_present(int fb_w, int fb_h);

#endif
