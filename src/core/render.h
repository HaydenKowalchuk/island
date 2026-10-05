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

#define R_MAX_VERTS 16384
#define R_MAX_DRAWS 512

typedef struct {
    float x, y, z;
    float r, g, b, a;
    float u, v;
} RVertex;

typedef enum { R_TRIANGLES, R_LINES, R_QUADS } RPrim;
typedef enum { R_CULL_NONE, R_CULL_BACK } RCull;

/* Scene pass a draw belongs to, so per-part costs can be counted. */
typedef enum {
    R_PASS_OTHER,
    R_PASS_SKY,
    R_PASS_OCEAN,
    R_PASS_TERRAIN,
    R_PASS_PROPS,
    R_PASS_VEHICLE,
    R_PASS_PARTICLES,
    R_PASS_HUD,
    R_PASS_COUNT
} RPass;

/*
 * How a draw must be composited: what the draw *is*, in terms the hardware can
 * sort and discard. This is the declaration that lets a backend pick its native
 * list instead of inferring one from `blend` or the pass enum.
 *
 *   OPAQUE      solid geometry; sorts with the scene and writes depth
 *   CUTOUT      alpha-tested; sorts with the scene, per-pixel discard
 *   TRANSLUCENT alpha-blended over the solid pass, back to front
 *   OVERLAY     HUD/2D; drawn last, over everything, ignoring scene depth
 *
 * On the Dreamcast these map onto the three PVR polygon lists (OP, PT, TR).
 * OVERLAY has no PVR list of its own, so the backend shares PT with CUTOUT —
 * PT is the list the PVR renders after translucent, which is what "over
 * everything" needs. The core states the intent; which bin achieves it is the
 * backend's business, so no PVR/GE concepts leak into this header.
 */
typedef enum {
    R_COMPOSITE_OPAQUE,
    R_COMPOSITE_CUTOUT,
    R_COMPOSITE_TRANSLUCENT,
    R_COMPOSITE_OVERLAY,
    R_COMPOSITE_COUNT
} RComposite;

/* Defined in mesh.h. Forward-declared so RDraw can reference one. */
struct Mesh;

/*
 * One draw. Either a contiguous run of recorded vertices (`first`/`count`), or a
 * reference to a reusable `mesh` drawn with `world` (column-major). Mixing both
 * in one ordered list keeps pass/camera ordering intact.
 *
 * `screen_space` picks the projection for the vertices: a normalised [0,1] ortho
 * instead of the camera matrices. It is orthogonal to `composite` — the sky is
 * screen-space but still part of the scene (OPAQUE), while a HUD quad is
 * screen-space and OVERLAY. OVERLAY draws are always screen-space.
 */
typedef struct {
    RPrim prim;
    int first;
    int count;

    const struct Mesh* mesh;
    float world[16];

    RComposite composite; /* what the draw is; backends map this to a native list */
    int screen_space;     /* 1 = normalised [0,1] ortho, 0 = world space */
    int lit;
    int fog;
    int blend;
    int depth_write;
    RCull cull;
    int textured;
    float tint[4];

    RPass pass; /* filled in by r_draw from the current r_pass() */
} RDraw;

typedef struct {
    int width;  /* framebuffer pixels; used to place 2D text */
    int height;

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

    /* Submitted-vertex (index) and draw counts, total and per pass. */
    int stat_verts;
    int stat_draws;
    int pass_verts[R_PASS_COUNT];
    int pass_draws[R_PASS_COUNT];
} RenderFrame;

/* Recording API. The game calls these each frame. */
void r_frame_begin(void);
void r_clear(float r, float g, float b, float a);

/* Tells the recorder the framebuffer size, so pixel-based 2D text can place. */
void r_viewport(int width, int height);
void r_camera(const float* view, const float* proj);
void r_fog(int enabled, const float color[4], float start, float end);
int  r_vertex(float x, float y, float z,
              float r, float g, float b, float a, float u, float v);
void r_draw(const RDraw* draw);

/* Sets the pass subsequent draws are attributed to (for the stats counters). */
void r_pass(RPass pass);

/* Short fixed label for a pass, e.g. "OCEAN". */
const char* r_pass_name(RPass pass);

/* Short fixed label for a composite class, e.g. "TRANSLUCENT". */
const char* r_composite_name(RComposite composite);

/*
 * Draw a reusable mesh (see mesh.h) transformed by `world` (column-major, may
 * be NULL for identity). `tint` multiplies the mesh's baked colours and may be
 * NULL for white.
 */
void r_mesh(const struct Mesh* mesh, const float world[16], const float tint[4]);

/*
 * Camera-facing quad centred at `pos`, sized `w` x `h`, using the camera basis
 * from the last r_camera() call. Emits ordinary world-space triangles, so any
 * backend handles it without special support. Good for clouds and spray puffs.
 */
void r_billboard(const float pos[3], float w, float h, const float color[4]);

/*
 * Flat screen-space rectangle in normalised [0,1] coordinates, origin at the
 * bottom-left. Alpha-blended; `color[3]` controls opacity. HUD primitive.
 */
void r_rect_2d(float x, float y, float w, float h, const float color[4]);

/*
 * Flat ribbon through `n` world-space points (3 floats each), `width` across.
 * The strip is laid on the horizontal plane (its width runs perpendicular to
 * the path in XZ), so it reads as a wake or a road. Alpha-blended, fogged, and
 * batched into a single draw. Good for the car's wake and spray trails.
 */
void r_ribbon(const float* pts, int n, float width, const float color[4]);

/*
 * Text on the HUD. `x`/`y` are in pixels from the top-left, `scale` multiplies
 * the built-in 8x8 glyphs. Emits one batched, alpha-blended screen-space draw,
 * so a whole string costs a single draw. Uppercase renders best.
 */
void r_text_2d(int x, int y, float scale, const char* s, const float color[4]);

/* The frame recorded since r_frame_begin(). Read by the backend. */
const RenderFrame* r_frame(void);

#endif
