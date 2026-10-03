/*
 * GL-free smoke test for the RenderAPI (island1-1dtz).
 *
 * Core must be able to run the simulation and record a complete frame with no
 * backend linked at all. This stands in for a "recording stub backend": instead
 * of issuing draw calls it inspects the recorded RenderFrame, proving the API is
 * self-contained and that core has no platform/GL dependency.
 */
#include <math.h>
#include <stdio.h>

#include "game.h"
#include "mesh.h"
#include "render.h"
#include "terrain.h"

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
    int terrain_verts = f->pass_verts[R_PASS_TERRAIN];

    /* Terrain (island1-39q6/xf8m): a hill at the centre, open sea off-map. */
    float hill = terrain_height_at(0.0f, 78.0f);
    float offmap = terrain_height_at(0.0f, 0.0f);
    float nrm[3];
    terrain_normal_at(0.0f, 78.0f, nrm);
    float nlen = sqrtf(nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2]);
    printf("api-smoke: terrain hill=%.1f offmap=%.1f n=(%.2f,%.2f,%.2f)\n", hill,
           offmap, nrm[0], nrm[1], nrm[2]);

    game_shutdown();

    /* The scene must produce geometry, at least one mesh draw (the car) and at
     * least one screen-space draw (the sky). */
    if (f->vert_count <= 0 || f->draw_count <= 0)
        return 1;
    if (mesh_draws == 0 || screen_draws == 0)
        return 1;
    if (ocean_verts <= 0 || vehicle_verts <= 0 || terrain_verts <= 0)
        return 1;
    if (hill <= 1.0f || offmap > -100.0f)
        return 1;
    if (nlen < 0.99f || nlen > 1.01f || nrm[1] < 0.5f)
        return 1;

    return 0;
}
