#include "render_gl.h"

#include <string.h>

#include <glad/glad.h>

#include "gfx_gl.h"
#include "render.h" /* core RenderAPI: r_frame(), RenderFrame, RDraw, RVertex */

void render_init(void) {
    gfx_init();
    glClearColor(SKY_COLOR_R, SKY_COLOR_G, SKY_COLOR_B, 1.0f);
    glClearDepth(1.0);
}

void render_shutdown(void) {
    /* No GL objects to release yet; textures and meshes will clean up here. */
}

static void set_projection(const float* mat) {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMultMatrixf(mat);
}

static void set_modelview(const float* mat) {
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glMultMatrixf(mat);
}

static void draw_state_from(const RenderFrame* frame, const RDraw* d, GfxState* out) {
    out->lighting = d->lit;
    out->fog = d->fog && frame->fog_enabled;
    out->texture = d->textured;
    out->blend = d->blend;
    out->depth_write = d->depth_write;
    out->cull = (d->cull == R_CULL_BACK) ? GL_BACK : GL_NONE;
    out->filter = GL_NEAREST;
    out->tex_env = GL_MODULATE;
    out->fog_start = frame->fog_start;
    out->fog_end = frame->fog_end;
    memcpy(out->fog_color, frame->fog_color, sizeof(out->fog_color));
}

static GLenum gl_mode(RPrim prim) {
    switch (prim) {
        case R_LINES:  return GL_LINES;
        case R_QUADS:  return GL_QUADS;
        case R_TRIANGLES:
        default:       return GL_TRIANGLES;
    }
}

void render_present(int fb_w, int fb_h) {
    const RenderFrame* frame = r_frame();

    glViewport(0, 0, fb_w, fb_h);
    glClearColor(frame->clear[0], frame->clear[1], frame->clear[2], frame->clear[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    set_projection(frame->proj);
    set_modelview(frame->view);

    for (int i = 0; i < frame->draw_count; i++) {
        const RDraw* d = &frame->draws[i];

        GfxState gs;
        draw_state_from(frame, d, &gs);
        gfx_push(&gs);

        if (d->screen_space)
            gfx_screen_begin_unit();

        glBegin(gl_mode(d->prim));
        for (int v = 0; v < d->count; v++) {
            const RVertex* vert = &frame->verts[d->first + v];
            float r = vert->r * d->tint[0];
            float g = vert->g * d->tint[1];
            float b = vert->b * d->tint[2];
            float a = vert->a * d->tint[3];

            if (d->textured)
                glTexCoord2f(vert->u, vert->v);
            glColor4f(r, g, b, a);
            glVertex3f(vert->x, vert->y, vert->z);
        }
        glEnd();

        if (d->screen_space)
            gfx_screen_end();

        gfx_pop();
    }
}
