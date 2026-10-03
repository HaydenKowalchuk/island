#include "texture_dc.h"

#include <stdio.h>
#include <stdlib.h>

#include <dc/pvr.h>

/*
 * PNG only keeps the vendored decoder small; all assets are 8-bit RGBA PNGs.
 * Shared with the desktop backend: the desktop dir is on the include path for
 * this header only. STBI_ONLY_PNG leaves unused helpers in the header, so
 * silence that third-party warning for the include only.
 */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "texture.h"

/*
 * Dreamcast texture loader. Resolves a name to <ASSET_DIR>/<name>, decodes it
 * with stb_image, converts to a 16-bit PVR format and uploads to VRAM twiddled
 * (pvr_txr_load_ex twiddles in the process). The 15/16-bit quantisation
 * matches the PS1 look on purpose (see docs/render-dc.md).
 */
#define MAX_TEXTURES 64

typedef struct {
    pvr_ptr_t vram;
    int w;
    int h;
    unsigned int fmt;
} DcTexture;

static DcTexture s_tex[MAX_TEXTURES];
static int s_tex_count = 0;

static const char* asset_root(void) {
    const char* dir = getenv("ASSET_DIR");
    return (dir && *dir) ? dir : "assets";
}

static TextureId load_texture(const char* name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", asset_root(), name);

    int w = 0, h = 0, comp = 0;
    unsigned char* data = stbi_load(path, &w, &h, &comp, 4);
    if (!data) {
        fprintf(stderr, "texture: cannot load %s (%s)\n", path, stbi_failure_reason());
        return 0;
    }

    /*
     * Detect alpha: masked textures (canopies, cutouts) need 1-bit alpha, so
     * they pack as ARGB1555; opaque tiles pack as RGB565. Both are 2 bytes
     * per pixel.
     */
    int has_alpha = 0;
    for (int i = 0; i < w * h; i++) {
        if (data[i * 4 + 3] < 255) {
            has_alpha = 1;
            break;
        }
    }

    unsigned short* pixels = (unsigned short*)malloc((size_t)w * (size_t)h * 2);
    if (!pixels) {
        stbi_image_free(data);
        return 0;
    }

    for (int i = 0; i < w * h; i++) {
        unsigned char r = data[i * 4 + 0];
        unsigned char g = data[i * 4 + 1];
        unsigned char b = data[i * 4 + 2];
        if (has_alpha) {
            unsigned int a = data[i * 4 + 3] >= 128 ? 0x8000u : 0x0000u;
            pixels[i] = (unsigned short)(a | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
        } else {
            pixels[i] = (unsigned short)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }

    pvr_ptr_t vram = pvr_mem_malloc((size_t)w * (size_t)h * 2);
    if (!vram) {
        fprintf(stderr, "texture: no PVR VRAM for %s\n", path);
        free(pixels);
        stbi_image_free(data);
        return 0;
    }

    /* pvr_txr_load_ex twiddles the linear buffer and uploads it. */
    pvr_txr_load_ex(pixels, vram, (unsigned int)w, (unsigned int)h,
                    PVR_TXRLOAD_16BPP);

    free(pixels);
    stbi_image_free(data);

    if (s_tex_count >= MAX_TEXTURES) {
        pvr_mem_free(vram);
        return 0;
    }

    Entry* e = &s_tex[s_tex_count++];
    e->vram = vram;
    e->w = w;
    e->h = h;
    e->fmt = has_alpha ? (PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_TWIDDLED)
                       : (PVR_TXRFMT_RGB565 | PVR_TXRFMT_TWIDDLED);

    /* Handles are registry indices; 0 stays "no texture". */
    return (TextureId)s_tex_count;
}

void texture_dc_init(void) {
    texture_set_loader(load_texture);
}

void texture_dc_shutdown(void) {
    for (int i = 0; i < s_tex_count; i++)
        pvr_mem_free(s_tex[i].vram);
    s_tex_count = 0;
}

const DcTexture* texture_dc_lookup(int id) {
    if (id <= 0 || id > s_tex_count)
        return NULL;
    return &s_tex[id - 1];
}
