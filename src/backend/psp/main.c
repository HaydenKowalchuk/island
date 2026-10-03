/*
 * island -- PSP backend
 *
 * Owns the GE (sceGu) display, the controller and the fixed-timestep loop. Each
 * frame it polls the pad into the core input contract, advances the core
 * simulation, records a frame through the RenderAPI and presents it on the GE.
 *
 * Controls (island1-rggc):
 *   Cross throttle   Circle brake      L boost        Square interact
 *   Triangle car     Start pause       Home quit      nub steer / pan
 */

#include <stdio.h>

#include <pspdisplay.h>
#include <pspgu.h>
#include <pspkernel.h>

#include "game.h"
#include "input.h"
#include "render_gu.h"

#include "psp_setup.h"

#define SCR_W 480
#define SCR_H 272

/* Fixed logic tick. Physics and game rules run at exactly this rate. */
#define TICK_HZ 60.0f
#define TICK (1.0f / TICK_HZ)

/* Cap on catch-up ticks per frame; beyond this the backlog is dropped. */
#define MAX_TICKS 5

/* Longest frame delta we accept. */
#define MAX_FRAME_DT 0.25f

static float now_seconds(void) {
    return (float)((double)sceKernelGetSystemTimeWide() / 1000000.0);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    psp_setup_callbacks();
    setbuf(stdout, NULL);

    input_init();
    backend_input_init();

    game_resize(SCR_W, SCR_H);

    /* render_init installs the texture loader, so content built in game_init
     * can request textures and have them uploaded immediately. */
    render_init();
    game_init();

    float last = now_seconds();
    float accumulator = 0.0f;

    while (!psp_done) {
        float now = now_seconds();
        float frame_dt = now - last;
        last = now;

        if (frame_dt < 0.0f)
            frame_dt = 0.0f;
        if (frame_dt > MAX_FRAME_DT)
            frame_dt = MAX_FRAME_DT;

        backend_input_poll();
        if (input_held(ACT_QUIT))
            break;

        accumulator += frame_dt;

        int ticks = 0;
        while (accumulator >= TICK && ticks < MAX_TICKS) {
            input_begin_frame();

            game_tick(TICK);

            accumulator -= TICK;
            ticks++;
        }

        if (ticks == MAX_TICKS)
            accumulator = 0.0f; /* Drop the backlog rather than spiral. */

        game_render(frame_dt);
        render_present(SCR_W, SCR_H);
    }

    game_shutdown();
    render_shutdown();
    sceKernelExitGame();
    return 0;
}
