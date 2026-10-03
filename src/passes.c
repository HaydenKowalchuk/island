#include "passes.h"

#include "gfx.h"
#include "render.h"

/*
 * Base state for world geometry: lit, fogged, depth-tested, NEAREST textures.
 * Every world pass starts from this and pushes only what it changes.
 */
static GfxState world_state(void) {
    GfxState s = *gfx_current();

    s.lighting = 1;
    s.fog = 1;
    s.texture = 1;
    s.blend = 0;
    s.depth_write = 1;
    s.cull = GL_BACK;
    s.filter = GL_NEAREST;
    s.tex_env = GL_MODULATE;
    s.fog_start = FOG_START;
    s.fog_end = FOG_END;
    s.fog_color[0] = SKY_COLOR_R;
    s.fog_color[1] = SKY_COLOR_G;
    s.fog_color[2] = SKY_COLOR_B;
    s.fog_color[3] = 1.0f;

    return s;
}

/*
 * Sky: a window-space quad with a vertical gradient, drawn before everything
 * else with depth writes off. The horizon band matches the fog colour so
 * distant terrain dissolves into the sky instead of ending at an edge.
 */
void pass_sky(void) {
    GfxState s = world_state();

    s.lighting = 0;
    s.fog = 0;
    s.texture = 0;
    s.depth_write = 0;
    s.cull = GL_NONE;

    gfx_push(&s);

    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    float x0 = (float)vp[0], y0 = (float)vp[1];
    float x1 = (float)(vp[0] + vp[2]), y1 = (float)(vp[1] + vp[3]);

    gfx_screen_begin(vp[2], vp[3]);

    glBegin(GL_QUADS);
    glColor3f(SKY_COLOR_R, SKY_COLOR_G, SKY_COLOR_B);
    glVertex2f(x0, y0);
    glVertex2f(x1, y0);
    glColor3f(0.20f, 0.48f, 0.86f);
    glVertex2f(x1, y1);
    glVertex2f(x0, y1);
    glEnd();

    gfx_screen_end();

    gfx_pop();
}

/*
 * Ocean: a large quad at sea level with a scrolling water texture. Terrain task
 * island1-iqtz owns the geometry and UV animation; until then this pass just
 * declares the state it will need.
 */
void pass_ocean(void) {
    GfxState s = world_state();
    gfx_push(&s);
    gfx_pop();
}

/*
 * Terrain: low-poly island mesh with hard textures, built from client-state
 * vertex arrays. See island1-xf8m.
 *
 * Until the mesh exists this draws the scaffold's reference grid and origin
 * gizmo, so the camera, fog and fixed-function lighting stay verifiable.
 */
static void debug_grid(void) {
    const float half = 20.0f, step = 1.0f;

    glDisable(GL_LIGHTING);
    glLineWidth(1.0f);

    for (float x = -half; x <= half; x += step) {
        if (x == 0.0f) continue;
        glColor3f(0.35f, 0.38f, 0.34f);
        glBegin(GL_LINES);
        glVertex3f(x, 0.0f, -half);
        glVertex3f(x, 0.0f, half);
        glEnd();
    }
    for (float z = -half; z <= half; z += step) {
        if (z == 0.0f) continue;
        glColor3f(0.35f, 0.38f, 0.34f);
        glBegin(GL_LINES);
        glVertex3f(-half, 0.0f, z);
        glVertex3f(half, 0.0f, z);
        glEnd();
    }
}

static void debug_axes(float len) {
    glDisable(GL_LIGHTING);
    glLineWidth(2.0f);

    glBegin(GL_LINES);
    glColor3f(1.0f, 0.2f, 0.2f); glVertex3f(0.0f, 0.0f, 0.0f); glVertex3f(len, 0.0f, 0.0f);
    glColor3f(0.2f, 1.0f, 0.2f); glVertex3f(0.0f, 0.0f, 0.0f); glVertex3f(0.0f, len, 0.0f);
    glColor3f(0.2f, 0.2f, 1.0f); glVertex3f(0.0f, 0.0f, 0.0f); glVertex3f(0.0f, 0.0f, len);
    glEnd();

    glLineWidth(1.0f);
    glEnable(GL_LIGHTING);
}

void pass_terrain(void) {
    GfxState s = world_state();
    gfx_push(&s);

    debug_grid();
    debug_axes(4.0f);

    gfx_pop();
}

void pass_props(void) {
    GfxState s = world_state();
    gfx_push(&s);
    gfx_pop();
}

void pass_buildings(void) {
    GfxState s = world_state();
    gfx_push(&s);
    gfx_pop();
}

void pass_vehicle(void) {
    GfxState s = world_state();
    gfx_push(&s);
    gfx_pop();
}

/* Particles: blended, depth-tested but not depth-written, double-sided. */
void pass_particles(void) {
    GfxState s = world_state();

    s.blend = 1;
    s.depth_write = 0;
    s.cull = GL_NONE;

    gfx_push(&s);
    gfx_pop();
}

/* Beacon: translucent column, additive-ish blend, visible through terrain. */
void pass_beacon(void) {
    GfxState s = world_state();

    s.blend = 1;
    s.depth_write = 0;
    s.cull = GL_NONE;

    gfx_push(&s);
    gfx_pop();
}

/* HUD: window-space overlay, unlit and untextured. See island1-n7to. */
void pass_hud(void) {
    GfxState s = world_state();

    s.lighting = 0;
    s.fog = 0;
    s.texture = 0;
    s.depth_write = 0;
    s.cull = GL_NONE;

    gfx_push(&s);
    gfx_pop();
}