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
#include "menu.h"
#include "mesh.h"
#include "render.h"
#include "terrain.h"

int main(void) {
    game_init();
    /* Gameplay tests jump straight into PLAY (the boot state is INTRO). */
    menu_set_screen(MENU_SCREEN_PLAY);
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

    /*
     * Composite classes (island1-m4g4): every draw must declare what it is so a
     * backend can pick a native list instead of inferring one. The load-bearing
     * case is that screen_space alone does not mean HUD — the sky is screen-space
     * yet still sorts and depth-tests as scene geometry, while HUD rects and text
     * are overlays.
     */
    int comp_draws[R_COMPOSITE_COUNT];
    for (int i = 0; i < R_COMPOSITE_COUNT; i++)
        comp_draws[i] = 0;
    int hud_overlay = 0, hud_not_overlay = 0;
    int sky_overlay = 0, sky_not_opaque = 0;
    int translucent_not_blend = 0, overlay_not_screen = 0;
    for (int i = 0; i < f->draw_count; i++) {
        const RDraw* d = &f->draws[i];
        if (d->composite < 0 || d->composite >= R_COMPOSITE_COUNT)
            return 1;
        comp_draws[d->composite]++;

        if (d->pass == R_PASS_HUD) {
            if (d->composite == R_COMPOSITE_OVERLAY)
                hud_overlay++;
            else
                hud_not_overlay++;
            if (!d->screen_space)
                overlay_not_screen++;
        }
        if (d->pass == R_PASS_SKY) {
            if (d->composite == R_COMPOSITE_OVERLAY)
                sky_overlay++;
            else
                sky_not_opaque++;
        }
        if (d->composite == R_COMPOSITE_TRANSLUCENT && !d->blend)
            translucent_not_blend++;
    }
    printf("api-smoke: composite");
    for (int i = 0; i < R_COMPOSITE_COUNT; i++)
        printf(" %s=%d", r_composite_name((RComposite)i), comp_draws[i]);
    printf("\n");

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

    /* HUD flag (island1-fovq): off drops only the HUD pass; scene stays. */
    int hud_on = f->pass_draws[R_PASS_HUD];
    game_set_hud(0);
    game_render(1.0f / 60.0f);
    int hud_off = f->pass_draws[R_PASS_HUD];
    int scene_off = f->pass_verts[R_PASS_VEHICLE] + f->pass_verts[R_PASS_TERRAIN];
    game_set_hud(1);
    printf("api-smoke: hud draws on=%d off=%d scene=%d\n", hud_on, hud_off,
           scene_off);

    /* Prop LOD (island1-1927): a far camera culls all distant props. */
    int props_near = f->pass_draws[R_PASS_PROPS];
    game_place_vehicle(-200.0f, -200.0f, 0.0f);
    game_render(1.0f / 60.0f);
    int props_far = f->pass_draws[R_PASS_PROPS];
    int veh_far = f->pass_verts[R_PASS_VEHICLE];
    printf("api-smoke: lod props near=%d far=%d veh=%d\n", props_near, props_far,
           veh_far);

    game_shutdown();

    /* The scene must produce geometry, at least one mesh draw (the car) and at
     * least one screen-space draw (the sky). */
    if (f->vert_count <= 0 || f->draw_count <= 0)
        return 1;
    if (mesh_draws == 0 || screen_draws == 0)
        return 1;
    if (ocean_verts <= 0 || vehicle_verts <= 0 || terrain_verts <= 0)
        return 1;
    if (hud_on <= 0 || hud_off != 0 || scene_off <= 0)
        return 1;
    if (props_near <= 0 || props_far != 0 || veh_far <= 0)
        return 1;
    if (hill <= 1.0f || offmap > -100.0f)
        return 1;
    if (nlen < 0.99f || nlen > 1.01f || nrm[1] < 0.5f)
        return 1;

    /* Composite declarations must be present and consistent (island1-m4g4). */
    if (comp_draws[R_COMPOSITE_OPAQUE] == 0 || comp_draws[R_COMPOSITE_OVERLAY] == 0)
        return 1;
    if (hud_overlay == 0 || hud_not_overlay != 0 || overlay_not_screen != 0)
        return 1;
    if (sky_overlay != 0 || sky_not_opaque == 0 || translucent_not_blend != 0)
        return 1;

    return 0;
}
