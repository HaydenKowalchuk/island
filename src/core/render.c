#include "render.h"

#include <string.h>

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

const RenderFrame* r_frame(void) {
    return &s_frame;
}
