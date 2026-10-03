#ifndef ISLAND_BACKEND_PSP_RENDER_GU_H
#define ISLAND_BACKEND_PSP_RENDER_GU_H

/*
 * PSP GU (Graphics Engine) backend. Owns no game state: render_init() brings up
 * sceGu and installs the texture loader, then every frame render_present() turns
 * the frame recorded by core (r_frame()) into sceGu draw calls.
 */
void render_init(void);
void render_shutdown(void);
void render_present(int fb_w, int fb_h);

#endif
