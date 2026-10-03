#ifndef ISLAND_CORE_GAME_H
#define ISLAND_CORE_GAME_H

/*
 * Core game entry points. A backend owns the window/loop and drives the game:
 *
 *   game_init();
 *   game_resize(w, h);                     // on framebuffer resize
 *   per fixed tick: game_tick(TICK);       // after input_begin_frame()/input_poll()
 *   per frame:      game_render(frame_dt); // records the frame via the RenderAPI
 *
 * game_render() records primitives through the RenderAPI only. The backend then
 * presents whatever r_frame() holds. `frame_dt` is the real (clamped) frame
 * delta, used by the chase camera spring which integrates per render frame.
 */
void game_init(void);
void game_shutdown(void);
void game_resize(int width, int height);
void game_tick(float dt);
void game_render(float frame_dt);

#endif
