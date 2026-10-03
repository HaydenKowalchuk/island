/*
 * island -- Dreamcast backend
 *
 * Owns the video mode, the PVR and the main loop. Each frame it polls the
 * maple controller into the core input contract, advances the core simulation
 * on a fixed timestep, asks core to record a frame through the RenderAPI,
 * then presents that frame with the native PVR Tile Accelerator.
 *
 * Controls (input_dc.c): A throttle, B brake, L boost, Y interact, X vehicle,
 * stick/D-pad steer, Start pause, A+B+X+Y+Start quit.
 *
 * Build: meson setup build-dc --cross-file sh4-dreamcast-kos -Dplatform=dreamcast
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include <dc/video.h>
#include <kos.h>

#include "game.h"
#include "input.h"
#include "input_dc.h"
#include "render_dc.h"

#define SCREEN_W 640
#define SCREEN_H 480

/* Fixed logic tick. Physics and game rules run at exactly this rate. */
#define TICK_HZ 60.0f
#define TICK (1.0f / TICK_HZ)

/* Cap on catch-up ticks per frame; beyond this the backlog is dropped. */
#define MAX_TICKS 5

/* Longest frame delta we accept, to survive loading hiccups and breakpoints. */
#define MAX_FRAME_DT 0.25f

/*
 * Monotonic seconds. gettimeofday is the verified KOS pattern (see the
 * integrity project's sys_dreamcast.c); secbase keeps the values small.
 */
static float sys_time(void) {
    struct timeval tp;
    static int secbase = 0;

    gettimeofday(&tp, NULL);
    if (!secbase)
        secbase = tp.tv_sec;
    return (float)(tp.tv_sec - secbase) + (float)tp.tv_usec / 1000000.0f;
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    /* pvr_init requires a suitable 3D video mode to be active first. */
    vid_set_mode(DM_640x480, PM_RGB565);

    input_init();
    input_dc_init();

    game_resize(SCREEN_W, SCREEN_H);

    /* render_init initialises the PVR and installs the texture loader, so
     * content built in game_init can request textures and have them uploaded
     * to PVR VRAM immediately. */
    render_init();
    game_init();

    float last_time = sys_time();
    float accumulator = 0.0f;

    for (;;) {
        float now = sys_time();
        float frame_dt = now - last_time;
        last_time = now;

        if (frame_dt > MAX_FRAME_DT)
            frame_dt = MAX_FRAME_DT;

        backend_input_poll();

        /* Quit goes through the input manager like everything else. */
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
        render_present(SCREEN_W, SCREEN_H);
    }

    game_shutdown();
    render_shutdown();
    /* Returning from main exits to the KOS loader / boot menu. */
    return 0;
}
