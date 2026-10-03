#ifndef ISLAND_CORE_TEXTURE_H
#define ISLAND_CORE_TEXTURE_H

/*
 * Texture handles are opaque ints owned by the active backend. Core asks for a
 * texture by name; a backend-provided loader decodes and uploads it. This keeps
 * image decoding and GL out of core while still letting content reference
 * textures. 0 means "no texture".
 */
typedef int TextureId;
typedef TextureId (*TextureLoader)(const char* name);

/* The backend installs its loader once, before content requests textures. */
void texture_set_loader(TextureLoader loader);

/* Resolves `name` to a handle, calling the loader at most once per name. */
TextureId texture_get(const char* name);

#endif
