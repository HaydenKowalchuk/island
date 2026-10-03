#include "render.h"
#include "gfx.h"
#include "passes.h"

void render_init(void) {
    gfx_init();
    glClearColor(SKY_COLOR_R, SKY_COLOR_G, SKY_COLOR_B, 1.0f);
    glClearDepth(1.0);
}

void render_shutdown(void) {
    /* No GL objects to release yet; textures and meshes will clean up here. */
}

void render_set_projection(const float* mat) {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMultMatrixf(mat);
}

void render_set_modelview(const float* mat) {
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glMultMatrixf(mat);
}

void render_frame(void) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    pass_sky();
    pass_ocean();
    pass_terrain();
    pass_props();
    pass_buildings();
    pass_vehicle();
    pass_particles();
    pass_beacon();
    pass_hud();
}