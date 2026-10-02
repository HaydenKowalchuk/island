#include "render.h"

#include <cglm/cglm.h>

#define GRID_HALF_EXTENT 20.0f
#define GRID_STEP       1.0f

static void setup_light(GLenum light, const vec3 pos, const vec3 color) {
    GLfloat lp[4] = { pos[0], pos[1], pos[2], 1.0f };
    GLfloat lc[4] = { color[0], color[1], color[2], 1.0f };

    glLightfv(light, GL_POSITION, lp);
    glLightfv(light, GL_DIFFUSE, lc);
    glLightfv(light, GL_SPECULAR, lc);
    glLightfv(light, GL_AMBIENT, lc);

    glEnable(light);
}

void render_init(void) {
    glClearColor(0.44f, 0.68f, 0.84f, 1.0f);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    glShadeModel(GL_SMOOTH);

    /* OpenGL 1.2/1.5 fixed-function lighting: per-vertex with material. */
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_NORMALIZE);

    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_FALSE);
    glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL, GL_SEPARATE_SPECULAR_COLOR);
    glLightModeli(GL_LIGHT_MODEL_AMBIENT, GL_FALSE);

    setup_light(GL_LIGHT0, (vec3){ 0.5f, 1.0f, 0.3f }, (vec3){ 0.85f, 0.85f, 0.80f });
    setup_light(GL_LIGHT1, (vec3){ -0.6f, 0.4f, -0.5f }, (vec3){ 0.25f, 0.28f, 0.35f });

    /* OpenGL 1.4 per-fragment fog on top of fixed-function lighting. */
    GLfloat fog_color[4] = { 0.44f, 0.68f, 0.84f, 1.0f };
    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogfv(GL_FOG_COLOR, fog_color);
    glFogf(GL_FOG_START, 40.0f);
    glFogf(GL_FOG_END, 220.0f);
}

void render_shutdown(void) {
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_COLOR_MATERIAL);
    glDisable(GL_LIGHT0);
    glDisable(GL_LIGHT1);
}

void render_begin_frame(void) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
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

static void draw_grid_line(const vec3 a, const vec3 b) {
    glBegin(GL_LINES);
    glVertex3fv(a);
    glVertex3fv(b);
    glEnd();
}

void render_ground(void) {
    vec3 minor = { 0.35f, 0.38f, 0.34f };
    vec3 major = { 0.15f, 0.20f, 0.15f };

    glDisable(GL_LIGHTING);
    glLineWidth(1.0f);

    for (float x = -GRID_HALF_EXTENT; x <= GRID_HALF_EXTENT; x += GRID_STEP) {
        float m = (float)(int)x;
        if (m == 0.0f) continue;
        glColor3fv(major);
        vec3 a = { x, 0.0f, -GRID_HALF_EXTENT };
        vec3 b = { x, 0.0f, GRID_HALF_EXTENT };
        draw_grid_line(a, b);
    }

    for (float z = -GRID_HALF_EXTENT; z <= GRID_HALF_EXTENT; z += GRID_STEP) {
        float m = (float)(int)z;
        if (m == 0.0f) continue;
        glColor3fv(major);
        vec3 a = { -GRID_HALF_EXTENT, 0.0f, z };
        vec3 b = { GRID_HALF_EXTENT, 0.0f, z };
        draw_grid_line(a, b);
    }

    /* Highlight the 10-unit rings. */
    glColor3fv(minor);
    for (float t = 10.0f; t <= GRID_HALF_EXTENT; t += 10.0f) {
        vec3 a = { -t, 0.0f, -t };
        vec3 b = { t, 0.0f, t };
        vec3 c = { -t, 0.0f, t };
        vec3 d = { t, 0.0f, -t };
        draw_grid_line(a, b);
        draw_grid_line(c, d);
    }

    glEnable(GL_LIGHTING);
}

void render_axes(float len) {
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