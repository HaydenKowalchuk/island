#ifndef ISLAND_CORE_RENDER_H
#define ISLAND_CORE_RENDER_H

/*
 * RenderAPI: the only drawing interface core code is allowed to touch.
 *
 * Game code never issues gfx/GL calls. Instead it records primitives through the
 * r_* functions below; a backend (desktop now, PSP/Dreamcast later) reads the
 * recorded frame via r_frame() and turns it into native draw calls when it
 * presents. The recorded frame is plain data, so it is platform-agnostic.
 */

/* Art/game configuration. Sky doubles as fog colour so the horizon is seamless. */
#define SKY_COLOR_R  0.45f
#define SKY_COLOR_G  0.72f
#define SKY_COLOR_B  0.90f
#define SKY_ZENITH_R 0.20f
#define SKY_ZENITH_G 0.48f
#define SKY_ZENITH_B 0.86f

#define FOG_START 40.0f
#define FOG_END   220.0f

#define R_MAX_VERTS 8192
#define R_MAX_DRAWS 256

typedef struct {
    float x, y, z;
    float r, g, b, a;
    float u, v;
} RVertex;

typedef enum { R_TRIANGLES, R_LINES, R_QUADS } RPrim;
typedef enum { R_CULL_NONE, R_CULL_BACK } RCull;

/*
 * One draw: a contiguous run of vertices plus the state it must be drawn with.
 * `screen_space` draws use a normalised [0,1] ortho projection (HUD, sky);
 * otherwise vertices are in world space and the camera matrices apply.
 */
typedef struct {
    RPrim prim;
    int first;
    int count;
    int screen_space;
    int lit;
    int fog;
    int blend;
    int depth_write;
    RCull cull;
    int textured;
    float tint[4];
} RDraw;

typedef struct {
    float clear[4];
    float view[16]; /* column-major */
    float proj[16]; /* column-major */
    int fog_enabled;
    float fog_color[4];
    float fog_start;
    float fog_end;

    RVertex verts[R_MAX_VERTS];
    int vert_count;

    RDraw draws[R_MAX_DRAWS];
    int draw_count;
} RenderFrame;

/* Recording API. The game calls these each frame. */
void r_frame_begin(void);
void r_clear(float r, float g, float b, float a);
void r_camera(const float* view, const float* proj);
void r_fog(int enabled, const float color[4], float start, float end);
int  r_vertex(float x, float y, float z,
              float r, float g, float b, float a, float u, float v);
void r_draw(const RDraw* draw);

/* The frame recorded since r_frame_begin(). Read by the backend. */
const RenderFrame* r_frame(void);

#endif
