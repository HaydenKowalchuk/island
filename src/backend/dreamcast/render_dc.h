#ifndef ISLAND_BACKEND_DREAMCAST_RENDER_DC_H
#define ISLAND_BACKEND_DREAMCAST_RENDER_DC_H

/*
 * Dreamcast backend (native KOS PVR, Tile Accelerator — no GLdc, no GL).
 * It owns no game state: it initialises the PVR once, then every frame turns
 * the frame recorded by core (r_frame()) into PVR polygon lists and screen
 * space TA vertices. Contract mirrors the desktop backend; the full hardware
 * contract is docs/render-dc.md.
 */
void render_init(void);
void render_shutdown(void);
void render_present(int fb_w, int fb_h);

#endif
