#include "render.h"

#include <string.h>

#include "mesh.h"

/*
 * Platform-agnostic frame recorder. It only stores plain data; the backend is
 * responsible for turning it into real draw calls.
 */
static RenderFrame s_frame;

void r_frame_begin(void) {
    memset(&s_frame, 0, sizeof(s_frame));

    s_frame.clear[0] = SKY_COLOR_R;
    s_frame.clear[1] = SKY_COLOR_G;
    s_frame.clear[2] = SKY_COLOR_B;
    s_frame.clear[3] = 1.0f;

    s_frame.view[0] = s_frame.view[5] = s_frame.view[10] = s_frame.view[15] = 1.0f;
    s_frame.proj[0] = s_frame.proj[5] = s_frame.proj[10] = s_frame.proj[15] = 1.0f;

    s_frame.fog_color[3] = 1.0f;
}

void r_clear(float r, float g, float b, float a) {
    s_frame.clear[0] = r;
    s_frame.clear[1] = g;
    s_frame.clear[2] = b;
    s_frame.clear[3] = a;
}

void r_camera(const float* view, const float* proj) {
    memcpy(s_frame.view, view, 16 * sizeof(float));
    memcpy(s_frame.proj, proj, 16 * sizeof(float));
}

void r_fog(int enabled, const float color[4], float start, float end) {
    s_frame.fog_enabled = enabled;
    memcpy(s_frame.fog_color, color, 4 * sizeof(float));
    s_frame.fog_start = start;
    s_frame.fog_end = end;
}

int r_vertex(float x, float y, float z,
             float r, float g, float b, float a, float u, float v) {
    if (s_frame.vert_count >= R_MAX_VERTS)
        return -1;

    RVertex* vert = &s_frame.verts[s_frame.vert_count];
    vert->x = x; vert->y = y; vert->z = z;
    vert->r = r; vert->g = g; vert->b = b; vert->a = a;
    vert->u = u; vert->v = v;
    return s_frame.vert_count++;
}

void r_draw(const RDraw* draw) {
    if (s_frame.draw_count >= R_MAX_DRAWS)
        return;
    s_frame.draws[s_frame.draw_count++] = *draw;
}

void r_mesh(const struct Mesh* mesh, const float world[16], const float tint[4]) {
    if (!mesh || mesh->index_count == 0)
        return;

    RDraw d = { 0 };
    d.prim = R_TRIANGLES;
    d.mesh = mesh;
    d.fog = 1;
    d.depth_write = 1;
    d.cull = R_CULL_BACK;
    d.lit = 0; /* colours are baked; no runtime lighting on any backend */
    d.textured = (mesh->texture != 0);

    if (world) {
        memcpy(d.world, world, 16 * sizeof(float));
    } else {
        d.world[0] = d.world[5] = d.world[10] = d.world[15] = 1.0f;
    }

    if (tint) {
        memcpy(d.tint, tint, 4 * sizeof(float));
    } else {
        d.tint[0] = d.tint[1] = d.tint[2] = d.tint[3] = 1.0f;
    }

    r_draw(&d);
}

void r_billboard(const float pos[3], float w, float h, const float color[4]) {
    /* Camera right/up are the rows of the view rotation (column-major view). */
    const float* v = s_frame.view;
    float rx = v[0], ry = v[4], rz = v[8];
    float ux = v[1], uy = v[5], uz = v[9];
    float hw = w * 0.5f, hh = h * 0.5f;

    float a[3] = { pos[0] - rx * hw - ux * hh, pos[1] - ry * hw - uy * hh, pos[2] - rz * hw - uz * hh };
    float b[3] = { pos[0] + rx * hw - ux * hh, pos[1] + ry * hw - uy * hh, pos[2] + rz * hw - uz * hh };
    float c[3] = { pos[0] + rx * hw + ux * hh, pos[1] + ry * hw + uy * hh, pos[2] + rz * hw + uz * hh };
    float d[3] = { pos[0] - rx * hw + ux * hh, pos[1] - ry * hw + uy * hh, pos[2] - rz * hw + uz * hh };

    int first = r_vertex(a[0], a[1], a[2], color[0], color[1], color[2], color[3], 0.0f, 0.0f);
    r_vertex(b[0], b[1], b[2], color[0], color[1], color[2], color[3], 0.0f, 0.0f);
    r_vertex(c[0], c[1], c[2], color[0], color[1], color[2], color[3], 0.0f, 0.0f);
    r_vertex(d[0], d[1], d[2], color[0], color[1], color[2], color[3], 0.0f, 0.0f);

    RDraw draw = { 0 };
    draw.prim = R_QUADS;
    draw.first = first;
    draw.count = 4;
    draw.fog = 1;
    draw.blend = 1;
    draw.depth_write = 1;
    draw.cull = R_CULL_NONE;
    draw.tint[0] = draw.tint[1] = draw.tint[2] = draw.tint[3] = 1.0f;
    r_draw(&draw);
}

void r_rect_2d(float x, float y, float w, float h, const float color[4]) {
    int first = r_vertex(x, y, 0.0f, color[0], color[1], color[2], color[3], 0.0f, 0.0f);
    r_vertex(x + w, y, 0.0f, color[0], color[1], color[2], color[3], 0.0f, 0.0f);
    r_vertex(x + w, y + h, 0.0f, color[0], color[1], color[2], color[3], 0.0f, 0.0f);
    r_vertex(x, y + h, 0.0f, color[0], color[1], color[2], color[3], 0.0f, 0.0f);

    RDraw draw = { 0 };
    draw.prim = R_QUADS;
    draw.first = first;
    draw.count = 4;
    draw.screen_space = 1;
    draw.blend = 1;
    draw.depth_write = 0;
    draw.cull = R_CULL_NONE;
    draw.tint[0] = draw.tint[1] = draw.tint[2] = draw.tint[3] = 1.0f;
    r_draw(&draw);
}

const RenderFrame* r_frame(void) {
    return &s_frame;
}
