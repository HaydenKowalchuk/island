/*
 * GL-free smoke test for the RenderAPI (island1-1dtz).
 *
 * Core must be able to run the simulation and record a complete frame with no
 * backend linked at all. This stands in for a "recording stub backend": instead
 * of issuing draw calls it inspects the recorded RenderFrame, proving the API is
 * self-contained and that core has no platform/GL dependency.
 */
#include <stdio.h>

#include "game.h"
#include "mesh.h"
#include "render.h"

int main(void) {
    game_init();
    game_resize(320, 240);

    for (int i = 0; i < 3; i++)
        game_tick(1.0f / 60.0f);

    game_render(1.0f / 60.0f);

    const RenderFrame* f = r_frame();

    int mesh_draws = 0, raw_draws = 0, screen_draws = 0;
    for (int i = 0; i < f->draw_count; i++) {
        const RDraw* d = &f->draws[i];
        if (d->mesh)
            mesh_draws++;
        else
            raw_draws++;
        if (d->screen_space)
            screen_draws++;
    }

    printf("api-smoke: verts=%d draws=%d mesh=%d raw=%d screen=%d\n",
           f->vert_count, f->draw_count, mesh_draws, raw_draws, screen_draws);

    /* Per-pass submitted-vertex/draw counters (island1-eyai). */
    printf("api-smoke: stats total=%dv/%dd |", f->stat_verts, f->stat_draws);
    for (int i = 0; i < R_PASS_COUNT; i++) {
        if (f->pass_draws[i] == 0)
            continue;
        printf(" %s=%d/%d", r_pass_name((RPass)i), f->pass_verts[i], f->pass_draws[i]);
    }
    printf("\n");

    int ocean_verts = f->pass_verts[R_PASS_OCEAN];
    int vehicle_verts = f->pass_verts[R_PASS_VEHICLE];

    game_shutdown();

    /* The scene must produce geometry, at least one mesh draw (the car) and at
     * least one screen-space draw (the sky). */
    if (f->vert_count <= 0 || f->draw_count <= 0)
        return 1;
    if (mesh_draws == 0 || screen_draws == 0)
        return 1;
    if (ocean_verts <= 0 || vehicle_verts <= 0)
        return 1;

    return 0;
}
