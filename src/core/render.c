#include "render.h"

#include <math.h>
#include <string.h>

#include "font8x8.h"
#include "mesh.h"

/*
 * Platform-agnostic frame recorder. It only stores plain data; the backend is
 * responsible for turning it into real draw calls.
 */
static RenderFrame s_frame;

/* Persist across frames so text can place pixels regardless of call order. */
static int s_view_w = 0;
static int s_view_h = 0;

void r_viewport(int width, int height) {
    if (width > 0) s_view_w = width;
    if (height > 0) s_view_h = height;
    s_frame.width = s_view_w;
    s_frame.height = s_view_h;
}

void r_frame_begin(void) {
    memset(&s_frame, 0, sizeof(s_frame));
    s_frame.width = s_view_w;
    s_frame.height = s_view_h;

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

/*
 * Per-point half-width offset perpendicular to the path, in the XZ plane. Uses
 * the average of the neighbouring segments so a strip through shared points has
 * no gaps at the joins.
 */
static void ribbon_offset(const float* pts, int n, int i, float hw,
                          float* ox, float* oz) {
    int ia = (i > 0) ? i - 1 : i;
    int ib = (i < n - 1) ? i + 1 : i;
    float dx = pts[ib * 3 + 0] - pts[ia * 3 + 0];
    float dz = pts[ib * 3 + 2] - pts[ia * 3 + 2];
    float len = sqrtf(dx * dx + dz * dz);
    if (len < 1e-6f) {
        *ox = hw;
        *oz = 0.0f;
        return;
    }
    *ox = -dz / len * hw;
    *oz = dx / len * hw;
}

void r_ribbon(const float* pts, int n, float width, const float color[4]) {
    if (!pts || n < 2)
        return;

    const float hw = width * 0.5f;
    int first = -1;
    int count = 0;

    for (int i = 0; i < n - 1; i++) {
        float ax, az, bx, bz;
        ribbon_offset(pts, n, i, hw, &ax, &az);
        ribbon_offset(pts, n, i + 1, hw, &bx, &bz);

        const float* p = &pts[i * 3];
        const float* q = &pts[(i + 1) * 3];

        int v = r_vertex(p[0] - ax, p[1], p[2] - az, color[0], color[1], color[2], color[3], 0.0f, 0.0f);
        if (first < 0)
            first = v;
        r_vertex(p[0] + ax, p[1], p[2] + az, color[0], color[1], color[2], color[3], 0.0f, 0.0f);
        r_vertex(q[0] + bx, q[1], q[2] + bz, color[0], color[1], color[2], color[3], 0.0f, 0.0f);
        r_vertex(q[0] - bx, q[1], q[2] - bz, color[0], color[1], color[2], color[3], 0.0f, 0.0f);
        count += 4;
    }

    if (count == 0)
        return;

    RDraw draw = { 0 };
    draw.prim = R_QUADS;
    draw.first = first;
    draw.count = count;
    draw.fog = 1;
    draw.blend = 1;
    draw.depth_write = 0;
    draw.cull = R_CULL_NONE;
    draw.tint[0] = draw.tint[1] = draw.tint[2] = draw.tint[3] = 1.0f;
    r_draw(&draw);
}

void r_text_2d(int x, int y, float scale, const char* s, const float color[4]) {
    if (!s || s_frame.width <= 0 || s_frame.height <= 0 || scale <= 0.0f)
        return;

    const float inv_w = 1.0f / (float)s_frame.width;
    const float inv_h = 1.0f / (float)s_frame.height;
    float pen = (float)x;

    int first = -1;
    int count = 0;

    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch >= 128)
            ch = '?';
        const unsigned char* glyph = font8x8_basic[ch];

        for (int row = 0; row < 8; row++) {
            unsigned char bits = glyph[row];
            int col = 0;
            while (col < 8) {
                if (!(bits & (1u << col))) {
                    col++;
                    continue;
                }
                int run = 1;
                while (col + run < 8 && (bits & (1u << (col + run))))
                    run++;

                float gx = pen + (float)col * scale;
                float gy = (float)y + (float)row * scale;
                float nx = gx * inv_w;
                float ny = 1.0f - (gy + scale) * inv_h;
                float nw = (float)run * scale * inv_w;
                float nh = scale * inv_h;

                int v = r_vertex(nx, ny, 0.0f, color[0], color[1], color[2], color[3], 0.0f, 0.0f);
                if (first < 0)
                    first = v;
                r_vertex(nx + nw, ny, 0.0f, color[0], color[1], color[2], color[3], 0.0f, 0.0f);
                r_vertex(nx + nw, ny + nh, 0.0f, color[0], color[1], color[2], color[3], 0.0f, 0.0f);
                r_vertex(nx, ny + nh, 0.0f, color[0], color[1], color[2], color[3], 0.0f, 0.0f);
                count += 4;
                col += run;
            }
        }
        pen += 8.0f * scale;
    }

    if (count == 0)
        return;

    RDraw draw = { 0 };
    draw.prim = R_QUADS;
    draw.first = first;
    draw.count = count;
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
