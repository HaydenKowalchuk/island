#ifndef ISLAND_BACKEND_DREAMCAST_TEXTURE_DC_H
#define ISLAND_BACKEND_DREAMCAST_TEXTURE_DC_H

#include <dc/pvr.h>

/*
 * Dreamcast texture loader: decodes PNGs with the shared stb_image and
 * uploads to PVR VRAM (RGB565, or ARGB1555 when the PNG has alpha — masked
 * cutouts need it; island1-qb9x). Handles are registry indices; core caches
 * the name->handle map.
 */
typedef struct {
    pvr_ptr_t vram;   /* PVR VRAM pointer for pvr_poly_cxt_txr */
    int w;
    int h;
    unsigned int fmt; /* PVR_TXRFMT_* */
} DcTexture;

/* Installs the texture loader. Requires the PVR to be initialised. */
void texture_dc_init(void);

/* Frees every uploaded texture's VRAM. */
void texture_dc_shutdown(void);

/* Registry lookup for the render backend; NULL for 0 / unknown. */
const DcTexture* texture_dc_lookup(int id);

#endif
