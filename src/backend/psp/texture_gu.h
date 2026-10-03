#ifndef ISLAND_BACKEND_PSP_TEXTURE_GU_H
#define ISLAND_BACKEND_PSP_TEXTURE_GU_H

#include "texture.h"

/* Installs the PSP texture loader. Requires sceGu to be initialised. */
void texture_gu_init(void);

/* Releases every uploaded texture. */
void texture_gu_shutdown(void);

/*
 * Installs the GU texture state (texel format, image pointer, sampling) for a
 * handle returned by the loader. The caller still has to enable/disable
 * GU_TEXTURE_2D around the draw. A handle of 0 is a no-op.
 */
void texture_gu_apply(TextureId id);

#endif
