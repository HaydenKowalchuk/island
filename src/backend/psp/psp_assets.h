#ifndef ISLAND_BACKEND_PSP_ASSETS_H
#define ISLAND_BACKEND_PSP_ASSETS_H

#include <pspiofilemgr.h>

/*
 * Single asset-path resolver for the PSP backend (fixes the duplication
 * between texture_gu.c and audio_psp.c). main.c's boot_report() already
 * determines the boot kind once (psp_set_boot_kind) and chdir's disc boots
 * to USRDIR; everything else just needs the right root chain.
 *
 * PSP firmware has no environment variables, so there is no getenv/ASSET_DIR
 * override here — the boot kind fully determines the search order. All IO is
 * raw sceIo (stdio/fopen is sceIoOpen underneath, but relative-path + stdio
 * expansion is unreliable on disc boots, so we skip it entirely and read via
 * absolute sceIo paths where possible).
 */

/* Resolve the boot-kind root chain once, at startup (after main sets the
 * boot kind). Safe to call more than once; lookups also lazy-init. */
void psp_assets_init(void);

/* Open an asset by its path relative to the assets root, e.g.
 * "textures/parcel_32.png" or "audio/sfx/ui_move.wav". Returns a sceIo fd
 * (>= 0) on the first root that has it, or < 0 if none do. */
SceUID psp_asset_open(const char* rel);

/* Resolve rel to a full path in `out` (for APIs that want a filename).
 * Returns 1 if the asset exists (path left in out), 0 otherwise. */
int psp_asset_path(const char* rel, char* out, int out_len);

/* Read a whole asset into a malloc'd buffer via sceIo. Returns the buffer
 * (caller frees) and sets *out_size, or NULL on failure. */
void* psp_asset_read(const char* rel, unsigned int* out_size);

#endif
